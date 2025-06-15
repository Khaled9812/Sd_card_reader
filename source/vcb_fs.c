#include <stdlib.h>
#include <string.h>

#include "vcb_fs.h"
#include "ff.h"                 /* only for directory listing  */
#include "fsl_sd_disk.h"
#include "sdmmc_config.h"
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"

/* ───── global FatFs volume object (single instance) ───── */
static FATFS s_fs;

/* ───── existing helpers – unchanged ───────────────────── */
int vcb_fs_init(void)
{
    BOARD_SD_Config(&g_sd, NULL,
                    BOARD_SDMMC_SD_HOST_IRQ_PRIORITY, NULL);

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

    /* power-cycle once (NXP recommendation) */
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
    if (f_open(&f, path, FA_READ))
        return -1;

    uint32_t fsize = f_size(&f);
    uint8_t *buf   = (uint8_t *)malloc(fsize);
    if (!buf) { f_close(&f); return -1; }

    UINT br;
    FRESULT fr = f_read(&f, buf, fsize, &br);
    f_close(&f);

    if (fr || br != fsize)
    {
        free(buf);  return -1;
    }
    file->ptr = buf;  file->size = fsize;
    return 0;
}

/* ───── NEW: stream a file directly to memory ───────────── */
int vcb_fs_stream_to_addr(const char *path,
                          uint32_t    dest_addr,
                          uint32_t   *copied_bytes)
{
    FIL f;
    if (f_open(&f, path, FA_READ))
        return -1;

    static uint8_t sectorBuf[512];      /* scratch (uses BSS)        */
    UINT br; uint8_t *dst = (uint8_t *)dest_addr;
    uint32_t total = 0;

    do {
        FRESULT fr = f_read(&f, sectorBuf, sizeof sectorBuf, &br);
        if (fr != FR_OK) { f_close(&f); return -1; }

        memcpy(dst, sectorBuf, br);
        dst   += br;
        total += br;
    } while (br == sizeof sectorBuf);   /* last chunk < 512 ⇒ done   */

    f_close(&f);
    if (copied_bytes) *copied_bytes = total;
    return 0;
}

/* ───── NEW: hand-off execution to the loaded image ─────── */
__attribute__((noreturn))
void vcb_fs_jump(uint32_t app_base)
{
    uint32_t *vt = (uint32_t *)app_base;   /* vector table            */
    uint32_t  sp = vt[0];                  /* initial MSP             */
    uint32_t  pc = vt[1];                  /* reset handler           */

    SCB->VTOR = app_base;                  /* switch vectors          */
    __DSB(); __ISB();

    __set_MSP(sp);                         /* set stack pointer       */
    ((void (*)(void))(pc | 1u))();         /* branch to reset handler */

    /* should never get here */
    for (;;) { __NOP(); }
}
