/* Copyright (C) 2020, HENSOLDT Cyber GmbH */

/**
 * @file
 * @brief   Driver for the Secure Digital Host Controller
 */

#include "OS_Error.h"
#include "OS_Dataport.h"
#include "interfaces/if_OS_Storage.h"

#include "LibDebug/Debug.h"
#include "LibUtil/Bitmap.h"
#include "sdhc/mmc.h"
#include "compiler.h"

#include <stddef.h>
#include <string.h>

#include <camkes.h>
#include <camkes/io.h>

// these defines are at the moment a copy & paste from libdhcdrivers/src/sdhc.c
// in the future we may have them exported in a .h and included here
/* Present State Register */
#define PRES_STATE_DAT3         (1 << 23)
#define PRES_STATE_DAT2         (1 << 22)
#define PRES_STATE_DAT1         (1 << 21)
#define PRES_STATE_DAT0         (1 << 20)
#define PRES_STATE_WPSPL        (1 << 19) //Write Protect Switch Pin Level
#define PRES_STATE_CDPL         (1 << 18) //Card Detect Pin Level
#define PRES_STATE_CINST        (1 << 16) //Card Inserted
#define PRES_STATE_BWEN         (1 << 10) //Buffer Write Enable
#define PRES_STATE_RTA          (1 << 9)  //Read Transfer Active
#define PRES_STATE_WTA          (1 << 8)  //Write Transfer Active
#define PRES_STATE_SDSTB        (1 << 3)  //SD Clock Stable
#define PRES_STATE_DLA          (1 << 2)  //Data Line Active
#define PRES_STATE_CDIHB        (1 << 1)  //Command Inhibit(DATA)
#define PRES_STATE_CIHB         (1 << 0)  //Command Inhibit(CMD)

//------------------------------------------------------------------------------
enum
{
    InitFailBit_IO_OPS,
    InitFailBit_SDIO,
    InitFailBit_CINST,
    InitFailBit_MMC,
    InitFailBit_SDIRQ,

    InitFailBit_MAX = 8 /* Must not exceed 8 unless we change the size of
                           initFailBitmap in SdHostController_t */
}
InitFailBit_e;

typedef struct SdHostController
{
    sdio_host_dev_t     sdio;
    ps_io_ops_t         io_ops;
    mmc_card_t          mmc_card;
    OS_Dataport_t       port_storage;
    Bitmap8             initFailBitmap;
}
SdHostController_t;

#define NOT_INITIALIZED (-1)

static SdHostController_t ctx =
{
    .port_storage   = OS_DATAPORT_ASSIGN(storage_port),
    .initFailBitmap = NOT_INITIALIZED,
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

static inline
OS_Error_t
checkInit(SdHostController_t* ctx)
{
    if (Bitmap_GET_BIT(ctx->initFailBitmap, InitFailBit_CINST))
    {
        return OS_ERROR_DEVICE_NOT_PRESENT;
    }
    if (NOT_INITIALIZED == ctx->initFailBitmap)
    {
        return OS_ERROR_INVALID_STATE;
    }
    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
void
post_init(void)
{
    ctx.initFailBitmap = 0;

    int rslt = camkes_io_ops(&ctx.io_ops);
    if (0 != rslt)
    {
        Debug_LOG_ERROR("camkes_io_ops() failed: rslt = %i", rslt);
        Bitmap_SET_BIT(ctx.initFailBitmap, InitFailBit_IO_OPS);
        return;
    }

    rslt = sdio_init(
        peripheral_idx,
        &ctx.io_ops,
        &ctx.sdio);

    if (0 != rslt)
    {
        Debug_LOG_ERROR("sdio_init() failed: rslt = %i", rslt);
        Bitmap_SET_BIT(ctx.initFailBitmap, InitFailBit_SDIO);
        return;
    }

    // The Card detection pin setup is not supported yet on the i.MX6 SoloX, which
    // leads to the problem that calling the present state function will always
    // result in card not present, even if a card is inserted. Until this
    // functionality is available, this check will be skipped for this
    // particular platform.
#ifndef CONFIG_PLAT_NITROGEN6SX
    // Check SD card presence
    if (!(sdio_get_present_state(&ctx.sdio) & PRES_STATE_CINST))
    {
        Bitmap_SET_BIT(ctx.initFailBitmap, InitFailBit_CINST);
        Debug_LOG_INFO("%s: memory card not inserted", __func__);
        return;
    }
#endif

    Debug_LOG_DEBUG("Initializing SdHostController...");

    rslt = mmc_init(
               &ctx.sdio,
               &ctx.io_ops,
               &ctx.mmc_card);

    if (0 != rslt)
    {
        Bitmap_SET_BIT(ctx.initFailBitmap, InitFailBit_MMC);
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
        Bitmap_SET_BIT(ctx.initFailBitmap, InitFailBit_SDIRQ);
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
}

void irq_handle(void)
{
    if (OS_SUCCESS != checkInit(&ctx))
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
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
/**
 * @brief   Writes data to the storage.
 *
 * @note    Given data size and offset must be block size aligned!
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "written" never points to NULL.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_DEVICE_NOT_PRESENT - SD card is not present in the slot.
 * @retval  OS_ERROR_INVALID_STATE      - Initialization was unsuccessful.
 * @retval  OS_ERROR_INVALID_PARAMETER  - One of the given or storage parameters
 *                                        is invalid.
 * @retval  OS_ERROR_OUT_OF_BOUNDS      - Operation requested outside of the
 *                                        storage area.
 * @retval  OS_ERROR_ABORTED            - Failed to write all bytes.
 * @retval  OS_SUCCESS                  - Write was successful.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_write(
    off_t   const offset,   /**< [in]  Write start offset in bytes. */
    size_t  const size,     /**< [in]  Number of bytes to be written. Must be a
                                       multiple of the block size! */
    size_t* const written   /**< [out] Number of bytes written. */)
{
    Debug_LOG_DEBUG(
        "%s: offset = %" PRIiMAX ", size = %zu, *written = %zu",
        __func__,
        offset,
        size,
        *written);

    *written = 0U;

    OS_Error_t rslt = checkInit(&ctx);
    if (OS_SUCCESS != rslt)
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
        return rslt;
    }

    const size_t blockSz = getBlockSize(ctx.mmc_card);
    rslt = verifyParameters(
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
/**
 * @brief   Reads from the storage.
 *
 * @note    Given data size and offset must be block size aligned!
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "read" never points to NULL.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_DEVICE_NOT_PRESENT - SD card is not present in the slot.
 * @retval  OS_ERROR_INVALID_STATE      - Initialization was unsuccessful.
 * @retval  OS_ERROR_INVALID_PARAMETER  - One of the given or storage parameters
 *                                        is invalid.
 * @retval  OS_ERROR_OUT_OF_BOUNDS      - Operation requested outside of the
 *                                        storage area.
 * @retval  OS_ERROR_ABORTED            - Failed to read all bytes.
 * @retval  OS_SUCCESS                  - Read was successful.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_read(
    off_t   const offset,   /**< [in]  Read start offset in bytes. */
    size_t  const size,     /**< [in]  Number of bytes to be read. Must be a
                                       multiple of the block size! */
    size_t* const read      /**< [out] Number of bytes read. */)
{
    Debug_LOG_DEBUG(
        "%s: offset = %" PRIiMAX ", size = %zu, *read = %zu",
        __func__,
        offset,
        size,
        *read);

    *read = 0U;

    OS_Error_t rslt = checkInit(&ctx);
    if (OS_SUCCESS != rslt)
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
        return rslt;
    }

    const size_t blockSz = getBlockSize(ctx.mmc_card);
    rslt = verifyParameters(
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
/**
 * @brief   Erases given storage's memory area.
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "erased" never points to NULL.
 *
 * @todo    Investigate possible implementations.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_NOT_IMPLEMENTED - Not implemented yet.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_erase(
    off_t  const offset,    /**< [in]  Erase start offset in bytes. */
    off_t  const size,      /**< [in]  Number of bytes to be erased. */
    off_t* const erased     /**< [out] Number of bytes erased. */)
{
    *erased = 0U;

    return OS_ERROR_NOT_IMPLEMENTED;
}


//------------------------------------------------------------------------------
/**
 * @brief   Gets the storage size in bytes.
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "size" never points to NULL.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_DEVICE_NOT_PRESENT - SD card is not present in the slot.
 * @retval  OS_ERROR_INVALID_STATE      - Initialization was unsuccessful.
 * @retval  OS_SUCCESS                  - `size` is assigned.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_getSize(
    off_t* const size /**< [out] The size of the storage in bytes. */)
{
    OS_Error_t rslt = checkInit(&ctx);
    if (OS_SUCCESS != rslt)
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
        return rslt;
    }

    *size = getStorageSize(ctx.mmc_card);

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
/**
 * @brief   Gets the storage block size in bytes.
 *
 * @note    This driver only allows block-wise operation thus offset and size
 *          must be adjusted accordingly to this parameter.
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "blockSize" never points to NULL.
 *
 * @note    Block cannot be larger than what we can address in the memory
 *          thus it's type shall be `size_t`.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_DEVICE_NOT_PRESENT - SD card is not present in the slot.
 * @retval  OS_ERROR_INVALID_STATE      - Initialization was unsuccessful.
 * @retval  OS_SUCCESS                  - `blockSize` is assigned.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_getBlockSize(
    size_t* const blockSize /**< [out] The size of the block in bytes. */)
{
    OS_Error_t rslt = checkInit(&ctx);
    if (OS_SUCCESS != rslt)
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
        return rslt;
    }

    Debug_LOG_TRACE("%s: getting the block size...", __func__);

    *blockSize = getBlockSize(ctx.mmc_card);

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
/**
 * @brief   Gets the state of the storage.
 *
 * This function can be e.g. for detecting if the card is present in the slot.
 *
 * @note    This is a CAmkES RPC interface handler. It's guaranteed that
 *          "flags" never points to NULL.
 *
 * @return  An error code.
 *
 * @retval  OS_ERROR_DEVICE_NOT_PRESENT - SD card is not present in the slot.
 * @retval  OS_ERROR_INVALID_STATE      - Initialization was unsuccessful.
 * @retval  OS_ERROR_ACCESS_DENIED      - Failed to lock or unlock the mutex.
 * @retval  OS_SUCCESS                  - `flags` were assigned.
 */
OS_Error_t
NONNULL_ALL
storage_rpc_getState(
    uint32_t* flags /**< [out] Implementation specific flags marking the
                               state.*/)
{
    *flags = 0U;

    OS_Error_t rslt = checkInit(&ctx);
    if (OS_SUCCESS != rslt)
    {
        Debug_LOG_TRACE("%s: failed, initialization was unsuccessful.",
                        __func__);
        return rslt;
    }

    if (0 != clientMux_lock())
    {
        Debug_LOG_ERROR("%s: failed to lock mutex!", __func__);
        return OS_ERROR_ACCESS_DENIED;
    }

    if (Bitmap_GET_MASK(sdio_get_present_state(&ctx.sdio), PRES_STATE_CINST))
    {
        Bitmap_SET_BIT(*flags, OS_Storage_StateFlag_MEDIUM_PRESENT);
    }

// The Card detection pin setup is not supported yet on the i.MX6 SoloX, which
// leads to the problem that calling the present state function will always
// result in card not present, even if a card is inserted. Until this
// functionality is available, the call to the getState() function will always
// return card present for the i.MX6 SoloX.
#ifdef CONFIG_PLAT_NITROGEN6SX
    Bitmap_SET_BIT(*flags, OS_Storage_StateFlag_MEDIUM_PRESENT);
#endif

    if (0 != clientMux_unlock())
    {
        Debug_LOG_ERROR("%s: failed to unlock mutex!", __func__);
        return OS_ERROR_ACCESS_DENIED;
    }

    return OS_SUCCESS;
}
