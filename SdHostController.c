/*
 * Copyright (C) 2020, HENSOLDT Cyber GmbH
 */

#include "OS_Error.h"
#include "OS_Dataport.h"

#include "LibDebug/Debug.h"
#include "sdhc/mmc.h"
#include "compiler.h"

#include <stddef.h>
#include <string.h>

#include <camkes.h>
#include <camkes/io.h>


//------------------------------------------------------------------------------
typedef struct SdHostController
{
    sdio_host_dev_t     sdio;
    ps_io_ops_t         io_ops;
    mmc_card_t          mmc_card;
    bool                isInitilized;
    OS_Dataport_t       port_storage;
}
SdHostController_t;

static SdHostController_t ctx =
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
    DECL_UNUSED_VAR(char const* funcName),
    off_t   const offset,
    off_t   const size,
    size_t  const blockSz)
{
    // Both checks shall be performed, so that we get a better picture in case
    // of a failure.
    bool isValid = true;

    if (0 != (offset % blockSz))
    {
        Debug_LOG_ERROR("%s: "
            "Offset is not a multiplier of the blockSz: "
            "offset = %" PRIiMAX ", blockSz = %zu",
            funcName,
            offset,
            blockSz);

        isValid = false;
    }

    if (0 != (size % blockSz))
    {
        Debug_LOG_ERROR("%s: "
            "Size is not a multiplier of the blockSz: "
            "size = %" PRIiMAX ", blockSz = %zu",
            funcName,
            size,
            blockSz);

        isValid = false;
    }

    return isValid;
}

static
OS_Error_t
verifyParameters(
    char    const *funcName,
    off_t   const offset,
    off_t   const size,
    size_t  const blockSz,
    off_t   const storageSz)
{
    if ((offset < 0) || (size < 0) || (0U == blockSz) || (storageSz <= 0))
    {
        Debug_LOG_ERROR("%s: "
            "One of the parameters is out of the range (less or equal 0): "
            "offset = %" PRIiMAX ","
            "size = %" PRIiMAX ","
            "blockSz %zu,"
            "storageSz %" PRIiMAX,
            funcName,
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
        Debug_LOG_ERROR("%s: "
            "size %" PRIiMAX " exceeds dataport size %zu",
            funcName,
            size,
            dataport_size);

        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!areValidArguments(funcName, offset, size, blockSz))
    {
        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!isValidStorageArea(offset, size, storageSz))
    {
        Debug_LOG_ERROR("%s: "
            "Request outside of the storage area: offset = %" PRIiMAX ", "
            "size = %" PRIiMAX "",
            funcName,
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
    Debug_LOG_TRACE("%s: getting the card size...", __func__);

    // We are about to access the HW peripheral i.e. shared resource with the
    // irq_handle, so we need to take the possesion of it.
    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
        return 0;
    }

    const long long cardCapacity = mmc_card_capacity(mmcCard);

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
    }

    return (off_t)cardCapacity;
}

static
size_t
getBlockSize(mmc_card_t mmcCard)
{
    Debug_LOG_TRACE("%s: getting the card's block size...", __func__);

    // We are about to access the HW peripheral i.e. shared resource with the
    // irq_handle, so we need to take the possesion of it.
    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
        return 0;
    }

    const size_t blockSize = mmc_block_size(mmcCard);

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
    }

    return blockSize;
}

//------------------------------------------------------------------------------
void
post_init(void)
{
    int rslt = camkes_io_ops(&ctx.io_ops);
    if (0 != rslt)
    {
        Debug_LOG_ERROR("camkes_io_ops() failed: rslt = %i", rslt);
        return;
    }

    rslt = sdio_init(
        peripheral_idx,
        &ctx.io_ops,
        &ctx.sdio);

    if (0 != rslt)
    {
        Debug_LOG_ERROR("sdio_init() failed: rslt = %i", rslt);
        return;
    }

    Debug_LOG_DEBUG("Initializing SdHostController...");

    rslt = mmc_init(
               &ctx.sdio,
               &ctx.io_ops,
               &ctx.mmc_card);

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

    rslt = mmc_nth_irq(ctx.mmc_card, peripheral_idx);
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
        ctx.mmc_card,
        mmc_nth_irq(ctx.mmc_card, 0)))
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
        return OS_ERROR_INVALID_STATE;
    }

    const size_t blockSz = getBlockSize(ctx.mmc_card);
    const OS_Error_t rslt = verifyParameters(
        __func__,
        offset,
        size,
        blockSz,
        getStorageSize(ctx.mmc_card));

    if (OS_SUCCESS != rslt || (0U == size))
    {
        return rslt;
    }

    const unsigned long startBlock = offset / blockSz;
    const size_t        nBlocks    = ((size - 1) / blockSz) + 1;

    // TODO Underlying driver supports currently only 1 block operations even
    //      despite the interface claiming something different. As a workaround
    //      block by block operation will be executed.
    void*   storagePortOffset = OS_Dataport_getBuf(ctx.port_storage);
    size_t  writtenInLoop = 0U;
    long    writeResult = 0;

    for (unsigned long i = 0;
            i < nBlocks && writeResult >= 0;
            ++i)
    {
        const unsigned long blockToWrite = startBlock + i;

        Debug_LOG_TRACE("%s: "
            "writing block %lu... "
            "offset = %" PRIiMAX ", size = %zu, startBlock = %lu, nBlocks = %i",
            __func__,
            blockToWrite,
            offset,
            size,
            startBlock,
            nBlocks);

        // We are about to access the HW peripheral i.e. shared resource with the
        // irq_handle, so we need to take the possesion of it.
        if (0 != clientMux_lock())
        {
            Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
            break;
        }

        writeResult = mmc_block_write(
                        ctx.mmc_card,
                        blockToWrite,
                        1, // TODO Change to nBlocks.
                        storagePortOffset,
                        0,
                        NULL,
                        NULL);
        if (writeResult < 0)
        {
            Debug_LOG_ERROR("%s: "
                "write of block %lu failed: "
                "offset = %" PRIiMAX ", size = %zu, writeResult = %li",
                __func__,
                blockToWrite,
                offset,
                size,
                writeResult);
        }
        else
        {
            writtenInLoop += writeResult;
            Debug_LOG_TRACE("%s: written %zu out of %zu bytes.",
                            __func__, writtenInLoop, size);

            *written = writtenInLoop;
        }
        storagePortOffset += blockSz;

        if (0 != clientMux_unlock())
        {
            Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
            break;
        }
    }

    if (size != writtenInLoop)
    {
        Debug_LOG_WARNING("%s: could write only %zu bytes out of %zu",
            __func__, writtenInLoop, size);
        return OS_ERROR_ABORTED;
    }
    Debug_LOG_TRACE("%s: successfully written %zu bytes.",
                    __func__, writtenInLoop);
    return OS_SUCCESS;
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
        return OS_ERROR_INVALID_STATE;
    }

    const size_t blockSz = getBlockSize(ctx.mmc_card);
    const OS_Error_t rslt = verifyParameters(
        __func__,
        offset,
        size,
        blockSz,
        getStorageSize(ctx.mmc_card));

    if (OS_SUCCESS != rslt || (0U == size))
    {
        return rslt;
    }

    const unsigned long startBlock = offset / blockSz;
    const size_t        nBlocks    = ((size - 1) / blockSz) + 1;

    // TODO Underlying driver supports currently only 1 block operations even
    //      despite the interface claiming something different. As a workaround
    //      block by block operation will be executed.
    void*   storagePortOffset = OS_Dataport_getBuf(ctx.port_storage);
    size_t  readInLoop = 0U;
    long    readResult = 0;

    for (unsigned long i = 0;
            i < nBlocks && readResult >= 0;
            ++i)
    {
        const unsigned long blockToRead = startBlock + i;

        Debug_LOG_TRACE("%s: "
            "reading block %lu... "
            "offset = %" PRIiMAX ", size = %zu, startBlock = %lu, nBlocks = %i",
            __func__,
            blockToRead,
            offset,
            size,
            startBlock,
            nBlocks);

        // We are about to access the HW peripheral i.e. shared resource with the
        // irq_handle, so we need to take the possesion of it.
        if (0 != clientMux_lock())
        {
            Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
            break;
        }

        readResult = mmc_block_read(
                        ctx.mmc_card,
                        blockToRead,
                        1, // TODO Change to nBlocks.
                        storagePortOffset,
                        0,
                        NULL,
                        NULL);
        if (readResult < 0)
        {
            Debug_LOG_ERROR("%s: "
                "read of block %lu failed: "
                "offset = %" PRIiMAX ", size = %zu, readResult = %li",
                __func__,
                blockToRead,
                offset,
                size,
                readResult);
        }
        else
        {
            readInLoop += readResult;
            Debug_LOG_TRACE("%s: read %zu out of %zu bytes.",
                            __func__, readInLoop, size);

            *read = readInLoop;
        }
        storagePortOffset += blockSz;

        if (0 != clientMux_unlock())
        {
            Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
            break;
        }
    }

    if (size != readInLoop)
    {
        Debug_LOG_WARNING("%s: could read only %zu bytes out of %zu",
            __func__, readInLoop, size);
        return OS_ERROR_ABORTED;
    }
    Debug_LOG_TRACE("%s: successfully read %zu bytes.", __func__, readInLoop);
    return OS_SUCCESS;
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
    *erased = 0U;

    return OS_ERROR_NOT_IMPLEMENTED;
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
        return OS_ERROR_INVALID_STATE;
    }

    *size = getStorageSize(ctx.mmc_card);

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
        return OS_ERROR_INVALID_STATE;
    }

    Debug_LOG_TRACE("%s: getting the block size...", __func__);

    *blockSize = getBlockSize(ctx.mmc_card);

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
    *flags = 0U;
    if (!ctx.isInitilized)
    {
        Debug_LOG_ERROR("%s: initialization was unsuccessful", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
        return OS_ERROR_ACCESS_DENIED;
    }

    *flags = sdio_get_present_state_register(&ctx.sdio);

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
        return OS_ERROR_ACCESS_DENIED;
    }

    return OS_SUCCESS;
}
