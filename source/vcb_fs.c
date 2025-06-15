#include <stdlib.h>
#include <string.h>
#include "vcb_fs.h"
#include "ff.h"
#include "fsl_sd_disk.h"
#include "sdmmc_config.h"
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"

static FATFS s_fs;

int vcb_fs_init(void)
{
    BOARD_SD_Config(&g_sd, NULL, BOARD_SDMMC_SD_HOST_IRQ_PRIORITY, NULL);

    if (SD_HostInit(&g_sd) != kStatus_Success)
    {
        PRINTF("SD_HostInit failed\r\n");
        return -1;
    }
    if (SD_PollingCardInsert(&g_sd, kSD_Inserted) != kStatus_Success)
    {
        PRINTF("No card detected\r\n");
        return -1;
    }

    SD_SetCardPower(&g_sd, false);
    SD_SetCardPower(&g_sd, true);

    const TCHAR drv[] = { SDDISK + '0', ':', '/' };
    if (f_mount(&s_fs, drv, 0U))
    {
        PRINTF("f_mount failed\r\n");
        return -1;
    }
#if FF_FS_RPATH >= 2
    if (f_chdrive(drv))
    {
        PRINTF("f_chdrive failed\r\n");
        return -1;
    }
#endif
    return 0;
}

int vcb_fs_read_path(const char *path, VCB_FILE *file)
{
    FIL f;
    if (f_open(&f, path, FA_READ)) return -1;

    uint32_t fsize = f_size(&f);
    uint8_t *buf = (uint8_t *)malloc(fsize);
    if (!buf) { f_close(&f); return -1; }

    UINT br;
    FRESULT fr = f_read(&f, buf, fsize, &br);
    f_close(&f);

    if (fr || br != fsize)
    {
        free(buf);
        return -1;
    }

    file->ptr = buf;
    file->size = fsize;
    return 0;
}

int vcb_fs_stream_to_addr(const char *path, uint32_t dest_addr, uint32_t *copied_bytes)
{
    FIL f;
    if (f_open(&f, path, FA_READ)) return -1;

    static uint8_t sectorBuf[512];
    UINT br;
    uint8_t *dst = (uint8_t *)dest_addr;
    uint32_t total = 0;

    do {
        FRESULT fr = f_read(&f, sectorBuf, sizeof sectorBuf, &br);
        if (fr != FR_OK) { f_close(&f); return -1; }

        memcpy(dst, sectorBuf, br);
        dst += br;
        total += br;
    } while (br == sizeof sectorBuf);

    f_close(&f);
    if (copied_bytes) *copied_bytes = total;
    return 0;
}

int vcb_fs_stream_to_addr_from_file(FIL *f, uint32_t dest_addr, uint32_t length)
{
    static uint8_t buf[512];
    uint8_t *dst = (uint8_t *)dest_addr;
    UINT br;

    while (length)
    {
        uint32_t chunk = (length > sizeof(buf)) ? sizeof(buf) : length;
        if (f_read(f, buf, chunk, &br) != FR_OK || br != chunk) return -1;
        memcpy(dst, buf, chunk);
        dst += chunk;
        length -= chunk;
    }
    return 0;
}

int vcb_fs_stream_segment(FIL *f, uint32_t offset, uint32_t dest_addr, uint32_t length)
{
    if (f_lseek(f, offset)) return -1;
    return vcb_fs_stream_to_addr_from_file(f, dest_addr, length);
}

int vcb_fs_zero_fill(uint32_t dest_addr, uint32_t length)
{
    memset((void *)dest_addr, 0, length);
    return 0;
}

__attribute__((noreturn))
void vcb_fs_jump(uint32_t app_base)
{
    uint32_t *vt = (uint32_t *)app_base;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    SCB->VTOR = app_base;
    __DSB(); __ISB();

    __set_MSP(sp);
    ((void (*)(void))(pc | 1u))();

    for (;;) { __NOP(); }
}

uint32_t vcb_fs_read_u32_le(const uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}
