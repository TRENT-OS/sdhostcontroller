/*
 * Copyright (C) 2020, HENSOLDT Cyber GmbH
 */

#include "OS_Error.h"
#include "OS_Dataport.h"

#include "LibDebug/Debug.h"
#include "sdhc/mmc.h"

#include <stddef.h>
#include <string.h>

#include <camkes.h>
#include <camkes/io.h>


//------------------------------------------------------------------------------
typedef struct SdHostController
{
    sdio_host_dev_t  sdio;
    ps_io_ops_t      io_ops;
    mmc_card_t       mmc_card;
} SdHostController_t;

typedef struct SdHostController_Ctx
{
    bool                isInitilized;
    SdHostController_t  sdhc_ctx;
    OS_Dataport_t       port_storage;

} SdHostController_Ctx_t;

static SdHostController_Ctx_t ctx =
{
    .isInitilized  = false,
    .port_storage  = OS_DATAPORT_ASSIGN(storage_port)
};


//------------------------Private methods---------------------------------------
static
bool
isValidStorageArea(
    off_t const offset,
    off_t const size,
    off_t const storageSz)
{
    // Casting to the biggest possible integer for overflow detection purposes.
    uintmax_t const end = (uintmax_t)offset + (uintmax_t)size;

    // Checking integer overflow first. The end index is not part of the area,
    // but we allow offset = end with size = 0 here.
    //
    // We also do not accept negative offsets and sizes (`off_t` is signed).
    return ((offset >= 0)
            && (size >= 0)
            && (end >= offset)
            && (end <= storageSz));
}

static
bool
areValidArguments(
    off_t   const offset,
    off_t   const size,
    size_t  const blockSz)
{
    // Both checks shall be performed, so that we get a better picture in case
    // of a failure.
    bool isValid = true;

    if (0 != (offset % blockSz))
    {
        Debug_LOG_ERROR(
            "Offset is not a multiplier of the blockSz: "
            "offset = %" PRIiMAX ", blockSz = %zu",
            offset,
            blockSz);

        isValid = false;
    }

    if (0 != (size % blockSz))
    {
        Debug_LOG_ERROR(
            "Size is not a multiplier of the blockSz: "
            "size = %" PRIiMAX ", blockSz = %zu",
            size,
            blockSz);

        isValid = false;
    }

    return isValid;
}

static
OS_Error_t
verifyParameters(
    off_t   const offset,
    off_t   const size,
    size_t  const blockSz,
    off_t   const storageSz)
{
    if ((offset < 0) || (size < 0) || (0U == blockSz) || (storageSz <= 0))
    {
        Debug_LOG_ERROR(
            "One of the parameters is out of the range (less or equal 0): "
            "offset = %" PRIiMAX ","
            "size = %" PRIiMAX ","
            "blockSz %zu,"
            "storageSz %" PRIiMAX,
            offset,
            size,
            blockSz,
            storageSz);

        return OS_ERROR_INVALID_PARAMETER;
    }

    size_t dataport_size = OS_Dataport_getSize(ctx.port_storage);
    if (size > dataport_size)
    {
        // invalid request by the client, as it knows the data port size and
        // should never ask to write more data than port size
        Debug_LOG_ERROR(
            "size %" PRIiMAX " exceeds dataport size %zu",
            size,
            dataport_size);

        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!areValidArguments(offset, size, blockSz))
    {
        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!isValidStorageArea(offset, size, storageSz))
    {
        Debug_LOG_ERROR(
            "Request outside of the storage area: offset = %" PRIiMAX ", "
            "size = %" PRIiMAX "",
            offset,
            size);

        return OS_ERROR_OUT_OF_BOUNDS;
    }

    return OS_SUCCESS;
}

static
off_t
getStorageSize(mmc_card_t mmcCard)
{
    Debug_LOG_TRACE("Getting the card size...");

    // We are about to access the HW peripheral i.e. shared resource with the
    // irq_handle, so we need to take the possesion of it.
    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("Failed to lock mutex!");
        return 0;
    }

    const long long cardCapacity = mmc_card_capacity(mmcCard);

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("Failed to unlock mutex!");
    }

    if (0 == cardCapacity)
    {
        Debug_LOG_WARNING("SD card size is 0. Card not inserted?");
    }

    return (off_t)cardCapacity;
}

static
size_t
getBlockSize(mmc_card_t mmcCard)
{
    Debug_LOG_TRACE("Getting the card's block size...");

    // We are about to access the HW peripheral i.e. shared resource with the
    // irq_handle, so we need to take the possesion of it.
    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("Failed to lock mutex!");
        return 0;
    }

    const size_t blockSize = mmc_block_size(mmcCard);

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("Failed to unlock mutex!");
    }

    if (0U == blockSize)
    {
        Debug_LOG_ERROR("SD card's block size is 0.");
    }

    return blockSize;
}

//------------------------------------------------------------------------------
void
post_init(void)
{
    int rslt = camkes_io_ops(&ctx.sdhc_ctx.io_ops);
    if (0 != rslt)
    {
        Debug_LOG_ERROR("camkes_io_ops() failed: rslt = %i", rslt);
        return;
    }

    // TODO Do not pass sdio_default_id() here but relate the id to
    //      the paddr configuration passed in the main camkes file.
    rslt = sdio_init(
        peripheral_idx,
        &ctx.sdhc_ctx.io_ops,
        &ctx.sdhc_ctx.sdio);

    if (0 != rslt)
    {
        Debug_LOG_ERROR("sdio_init() failed: rslt = %i", rslt);
        return;
    }

    Debug_LOG_DEBUG("Initializing SdHostController...");

    rslt = mmc_init(
               &ctx.sdhc_ctx.sdio,
               &ctx.sdhc_ctx.io_ops,
               &ctx.sdhc_ctx.mmc_card);

    if (0 != rslt)
    {
        Debug_LOG_ERROR("mmc_init() failed: rslt = %i", rslt);
        return;
    }

    // Logic below is for informative purpose only, and is not required for the
    // proper initialization of the driver. Thanks to this client may verify if
    // proper IRQ number has been selected.
    Debug_LOG_TRACE(
        "Reading SD Controller #%i interrupt number.",
        peripheral_idx);

    rslt = mmc_nth_irq(ctx.sdhc_ctx.mmc_card, peripheral_idx);
    if (rslt < 0)
    {
        Debug_LOG_ERROR(
            "Could not detect SD Controller #%d IRQ. "
            "mmc_nth_irq() failed: rslt = %d",
            peripheral_idx,
            rslt);

        return;
    }

    Debug_LOG_TRACE(
        "SD Controller #%i interrupt is %i",
        peripheral_idx,
        rslt);

    ctx.isInitilized = true;
}

void irq_handle(void)
{
    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        goto irq_handle_exit;
    }

    // We are about to access the HW peripheral i.e. shared resource with the
    // rpc calls, so we need to take the possesion of it.
    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("Failed to lock mutex!");
        goto irq_handle_exit;
    }

    if (0 != mmc_handle_irq(
        ctx.sdhc_ctx.mmc_card,
        mmc_nth_irq(ctx.sdhc_ctx.mmc_card, 0)))
    {
        Debug_LOG_ERROR("No IRQ to handle!");
    }

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("Failed to unlock mutex!");
    }

irq_handle_exit:;
    const int rslt = irq_acknowledge();

    if (0 != rslt)
    {
        Debug_LOG_FATAL(
            "%s: sdhc irq_acknowledge() error, code %d",
            __func__,
            rslt);
    }

    return;
}

//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "written"
// never points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_write(
    off_t   const offset,
    size_t  const size,
    size_t* const written)
{
    Debug_LOG_DEBUG(
        "%s: offset = %" PRIiMAX ", size = %zu, *written = %zu",
        __func__,
        offset,
        size,
        *written);

    *written = 0U;

    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    const size_t blockSz = getBlockSize(ctx.sdhc_ctx.mmc_card);
    const OS_Error_t rslt = verifyParameters(
        offset,
        size,
        blockSz,
        getStorageSize(ctx.sdhc_ctx.mmc_card));

    if (OS_SUCCESS != rslt || (0U == size))
    {
        return rslt;
    }

    const unsigned long startBlock = offset / blockSz;
    const size_t        nBlocks    = ((size - 1) / blockSz) + 1;

    // TODO Underlying driver supports currently only 1 block operations even
    //      despite the interface claiming something different. As a workaround
    //      block by block operation will be executed.
    void* storagePortOffset = OS_Dataport_getBuf(ctx.port_storage);
    size_t writtenInLoop = 0U;

    for (unsigned long i = 0; i < nBlocks; ++i)
    {
        const unsigned long blockToWrite = startBlock + i;

        Debug_LOG_TRACE(
            "Writing block %lu... "
            "offset = %" PRIiMAX ", size = %zu, startBlock = %lu, nBlocks = %i",
            blockToWrite,
            offset,
            size,
            startBlock,
            nBlocks);

        long writeResult = -1;

        // We are about to access the HW peripheral i.e. shared resource with the
        // irq_handle, so we need to take the possesion of it.
        if (0 != clientMux_lock())
        {
            Debug_LOG_ERROR("Failed to lock mutex!");
        }
        else
        {
            writeResult = mmc_block_write(
                            ctx.sdhc_ctx.mmc_card,
                            blockToWrite,
                            1, // TODO Change to nBlocks.
                            storagePortOffset,
                            0,
                            NULL,
                            NULL);

            if (0 != clientMux_unlock())
            {
                Debug_LOG_ERROR("Failed to unlock mutex!");
            }
        }

        if (writeResult < 0)
        {
            Debug_LOG_ERROR(
                "Write of block %lu failed: "
                "offset = %" PRIiMAX ", size = %zu, writeResult = %li",
                blockToWrite,
                offset,
                size,
                writeResult);
        }
        else
        {
            writtenInLoop += writeResult;
            storagePortOffset += blockSz;
            Debug_LOG_TRACE("Written %zu out of %zu bytes.", writtenInLoop, size);
        }
    }

    Debug_LOG_DEBUG("Successfully written %zu bytes.", writtenInLoop);

    *written = writtenInLoop;
    return (size == writtenInLoop) ? OS_SUCCESS : OS_ERROR_GENERIC;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "read" never
// points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_read(
    off_t   const offset,
    size_t  const size,
    size_t* const read)
{
    Debug_LOG_DEBUG(
        "%s: offset = %" PRIiMAX ", size = %zu, *read = %zu",
        __func__,
        offset,
        size,
        *read);

    *read = 0U;

    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    const size_t blockSz = getBlockSize(ctx.sdhc_ctx.mmc_card);
    const OS_Error_t rslt = verifyParameters(
        offset,
        size,
        blockSz,
        getStorageSize(ctx.sdhc_ctx.mmc_card));

    if (OS_SUCCESS != rslt || (0U == size))
    {
        return rslt;
    }

    const unsigned long startBlock = offset / blockSz;
    const size_t        nBlocks    = ((size - 1) / blockSz) + 1;

    // TODO Underlying driver supports currently only 1 block operations even
    //      despite the interface claiming something different. As a workaround
    //      block by block operation will be executed.
    void* storagePortOffset = OS_Dataport_getBuf(ctx.port_storage);
    size_t readInLoop = 0U;

    for (unsigned long i = 0; i < nBlocks; ++i)
    {
        const unsigned long blockToRead = startBlock + i;

        Debug_LOG_TRACE(
            "Reading block %lu... "
            "offset = %" PRIiMAX ", size = %zu, startBlock = %lu, nBlocks = %i",
            blockToRead,
            offset,
            size,
            startBlock,
            nBlocks);

        long readResult = -1;

        // We are about to access the HW peripheral i.e. shared resource with the
        // irq_handle, so we need to take the possesion of it.
        if (0 != clientMux_lock())
        {
            Debug_LOG_ERROR("Failed to lock mutex!");
        }
        else
        {
            readResult = mmc_block_read(
                            ctx.sdhc_ctx.mmc_card,
                            blockToRead,
                            1, // TODO Change to nBlocks.
                            storagePortOffset,
                            0,
                            NULL,
                            NULL);

            if (0 != clientMux_unlock())
            {
                Debug_LOG_ERROR("Failed to unlock mutex!");
            }
        }

        if (readResult < 0)
        {
            Debug_LOG_ERROR(
                "Read of block %lu failed: "
                "offset = %" PRIiMAX ", size = %zu, readResult = %li",
                blockToRead,
                offset,
                size,
                readResult);
        }
        else
        {
            readInLoop += readResult;
            storagePortOffset += blockSz;
            Debug_LOG_TRACE("Read %zu out of %zu bytes.", readInLoop, size);
        }
    }

    Debug_LOG_DEBUG("Successfully read %zu bytes.", readInLoop);

    *read = readInLoop;
    return (size == readInLoop) ? OS_SUCCESS : OS_ERROR_GENERIC;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "erased" never
// points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_erase(
    off_t  const offset,
    off_t  const size,
    off_t* const erased)
{
    Debug_LOG_DEBUG(
        "%s: offset = %" PRIiMAX ", size = %" PRIiMAX ", *erased = %" PRIiMAX,
        __func__,
        offset,
        size,
        *erased);

    *erased = 0;

    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    const size_t blockSz = getBlockSize(ctx.sdhc_ctx.mmc_card);
    const OS_Error_t rslt = verifyParameters(
        offset,
        size,
        blockSz,
        getStorageSize(ctx.sdhc_ctx.mmc_card));

    if (OS_SUCCESS != rslt || (0U == size))
    {
        return rslt;
    }

    const unsigned long startBlock = offset / blockSz;
    const size_t        nBlocks    = ((size - 1) / blockSz) + 1;

    // TODO Underlying driver supports currently only 1 block operations despite
    //      the interface claiming something different. As a workaround
    //      by block operation will be executed.
    void* const storageDataPort = OS_Dataport_getBuf(ctx.port_storage);
    off_t erasedInLoop = 0;

    // 0xFF is the pattern that we use in the other storages for erasing, so
    // due to compatibility reasons here we follow the same pattern, however it
    // could be any other character or even erase could be left not implemented,
    // depending on the requirements. For now we stick with the 0xFF.
    memset(storageDataPort, 0xFF, blockSz);

    for (size_t i = 0; i < nBlocks; ++i)
    {
        const unsigned long blockToWrite = startBlock + i;

        Debug_LOG_TRACE(
            "Erasing block %lu... "
            "offset = %" PRIiMAX ", size = %zu, startBlock = %lu, nBlocks = %i",
            blockToWrite,
            offset,
            blockSz,
            startBlock,
            nBlocks);

        long writeResult = -1;

        // We are about to access the HW peripheral i.e. shared resource with the
        // irq_handle, so we need to take the possesion of it.
        if (0 != clientMux_lock())
        {
            Debug_LOG_ERROR("Failed to lock mutex!");
        }
        else
        {
            writeResult = mmc_block_write(
                            ctx.sdhc_ctx.mmc_card,
                            blockToWrite,
                            1, // TODO Change to nBlocks.
                            storageDataPort,
                            0,
                            NULL,
                            NULL);

            if (0 != clientMux_unlock())
            {
                Debug_LOG_ERROR("Failed to unlock mutex!");
            }
        }

        if (writeResult < 0)
        {
            Debug_LOG_ERROR(
                "Erase of block %lu failed: "
                "offset = %" PRIiMAX ", size = %" PRIiMAX ", writeResult = %li",
                blockToWrite,
                offset,
                size,
                writeResult);
        }
        else
        {
            erasedInLoop += writeResult;
            Debug_LOG_TRACE(
                "Erased %" PRIiMAX " out of %" PRIiMAX " bytes.",
                erasedInLoop,
                size);
        }
    }

    Debug_LOG_DEBUG("Successfully erased %" PRIiMAX " bytes.", erasedInLoop);

    *erased = erasedInLoop;
    return (size == erasedInLoop) ? OS_SUCCESS : OS_ERROR_GENERIC;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "size" never
// points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_getSize(
    off_t* const size)
{
    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    Debug_LOG_TRACE("%s: Getting the size...", __func__);

    *size = getStorageSize(ctx.sdhc_ctx.mmc_card);

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "blockSize"
// never points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_getBlockSize(
    size_t* const blockSize)
{
    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    Debug_LOG_TRACE("%s: Getting the block size...", __func__);

    *blockSize = getBlockSize(ctx.sdhc_ctx.mmc_card);

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "flags" never
// points to NULL.
OS_Error_t
NONNULL_ALL
storage_rpc_getState(
    uint32_t* flags)
{
    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("Initialization was unsuccessful.");
        return OS_ERROR_INVALID_STATE;
    }

    *flags = 0U;
    Debug_LOG_ERROR("Request not supported.");

    return OS_ERROR_NOT_SUPPORTED;
}
