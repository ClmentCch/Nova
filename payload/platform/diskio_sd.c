/*
 * NOVA Boot - FatFs disk I/O for the SD card.
 *
 * NOVA only ever mounts the SD card. The hekate BDK diskio module also
 * implements eMMC/emuMMC/ramdisk volumes which NOVA does not need (and must
 * not touch), so this is a deliberately minimal SD-only implementation.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <bdk.h>
#include <libs/fatfs/diskio.h>

DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    /* sd_mount() already initialized the card before f_mount(). */
    return pdrv == 0 ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;
    if (!sd_get_card_initialized())
        return RES_NOTRDY;
    return sdmmc_storage_read(&sd_storage, (u32)sector, count, buff);
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;
    if (!sd_get_card_initialized())
        return RES_NOTRDY;
    return sdmmc_storage_write(&sd_storage, (u32)sector, count, (void *)buff);
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;

    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = sd_storage.sec_cnt;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 32768; /* 16 MiB erase block hint */
        return RES_OK;
    case CTRL_TRIM:
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
