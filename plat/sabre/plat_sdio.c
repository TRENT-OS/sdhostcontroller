/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright (C) 2021, HENSOLDT Cyber GmbH
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Original file at https://github.com/seL4/projects_libs/blob/master/libsdhcdrivers/src/plat/imx6/sdio.c
 */

#include <sdhc.h>
#include <services.h>

static const int
_sdhc_irq_table[] = {
    [SDHC1] = SDHC1_IRQ,
    [SDHC2] = SDHC2_IRQ,
    [SDHC3] = SDHC3_IRQ,
    [SDHC4] = SDHC4_IRQ
};

sdio_id_e sdio_default_id(void)
{
    return SDHC_DEFAULT;
}

int sdio_init(sdio_id_e id, ps_io_ops_t *io_ops, sdio_host_dev_t *dev)
{
    void *iobase;
    int ret;
    switch (id) {
    case SDHC1:
        iobase = RESOURCE(io_ops, SDHC1);
        break;
    case SDHC2:
        iobase = RESOURCE(io_ops, SDHC2);
        break;
    case SDHC3:
        iobase = RESOURCE(io_ops, SDHC3);
        break;
    case SDHC4:
        iobase = RESOURCE(io_ops, SDHC4);
        break;
    default:
        ZF_LOGE("SDHC ID is not supported!");
        return -1;
    }
    if (iobase == NULL) {
        ZF_LOGE("Failed to map device memory for SDHC");
        return -1;
    }

    ret = sdhc_init(iobase, _sdhc_irq_table, NSDHC, io_ops, dev);
    if (ret) {
        ZF_LOGE("Failed to initialise SDHC");
        return -1;
    }
    return 0;
}
