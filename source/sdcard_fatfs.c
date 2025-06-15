/**********************************************************************
 * i.MX RT117x  –  µSD-card ELF Loader
 *   · eDMA progress dots
 *   · application returns cleanly to the loader
 *********************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ff.h"
#include "vcb_fs.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "core_cm7.h"              /* for __get_MSP / __set_MSP */

/* ────────── board hooks ────────── */
extern void BOARD_ConfigMPU(void);
extern void BOARD_InitBootPins(void);
extern void BOARD_BootClockRUN(void);
extern void BOARD_InitDebugConsole(void);

/* ────────── constants & HW resources ────────── */
#define SCRATCH_SZ      512u
#define DMA_INST        DMA0
#define DMAMUX_INST     DMAMUX0
#define DMA_CH          0u          /* any free channel   */

/* ────────── NON-cacheable scratch (OCRAM, DMA-reachable) ────────── */
AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t scratch[SCRATCH_SZ], 32);

/* ────────── little-endian → u32 ────────── */
static inline uint32_t rd32le(const uint8_t *b)
{
    return (uint32_t)b[0] |
           ((uint32_t)b[1] <<  8) |
           ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

/* ────────── eDMA handle ────────── */
static edma_handle_t gDma;

/* ────────── DMA reachability (TCM is invisible) ────────── */
static bool dma_can_access(const void *p)
{
    uint32_t a = (uint32_t)p;
    if (a < 0x00080000u)                     return false; /* ITCM */
    if (a >= 0x20000000u && a < 0x20080000u) return false; /* DTCM */
    return true;                                         /* AXI  */
}

/* ────────── eDMA copy with live dots ────────── */
static void dma_memcpy(void *dst, const void *src, size_t len)
{
    edma_transfer_config_t cfg;
    EDMA_PrepareTransfer(&cfg, (void *)src, 1, (void *)dst, 1,
                         1, len, kEDMA_MemoryToMemory);
    EDMA_SubmitTransfer(&gDma, &cfg);

    PRINTF("      ↳ DMA %u B ", (unsigned)len);

    EDMA_StartTransfer(&gDma);
    uint32_t spin = 0;
    while (!(EDMA_GetChannelStatusFlags(DMA_INST, DMA_CH) & kEDMA_InterruptFlag))
    {
        if ((spin++ & 0x3FFFu) == 0u) PRINTF(".");
        __NOP();
    }
    PRINTF(" done\r\n");
    EDMA_ClearChannelStatusFlags(DMA_INST, DMA_CH, kEDMA_InterruptFlag);
}

/* ────────── universal copy helper ────────── */
static void blk_copy(void *dst, const void *src, size_t len)
{
    if (dma_can_access(dst) && dma_can_access(src) && len >= 16u)
        dma_memcpy(dst, src, len);
    else
        memcpy(dst, src, len);
}

/* ────────── call app & return ────────── */
static void call_app_and_return(uint32_t vt_addr)
{
    uint32_t loader_sp   = __get_MSP();        /* ← save current MSP   */
    uint32_t loader_vtor = SCB->VTOR;

    uint32_t *vt   = (uint32_t *)vt_addr;
    uint32_t app_sp    = vt[0];
    uint32_t app_entry = vt[1];

    __disable_irq();
    SCB->VTOR = vt_addr;
    __DSB(); __ISB();
    __set_MSP(app_sp);
    __enable_irq();

    ((void (*)(void))(app_entry | 1u))();      /* <- run application */

    /* === we’re back here when app returns === */
    __disable_irq();
    __set_MSP(loader_sp);                      /* restore loader MSP */
    SCB->VTOR = loader_vtor;                   /* restore vectors    */
    __DSB(); __ISB();
    __enable_irq();

    PRINTF("[i] Application returned to boot-loader.\r\n");
}

/* ────────── loader main ────────── */
int main(void)
{
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    edma_config_t dcfg;
    DMAMUX_Init(DMAMUX_INST);
    DMAMUX_SetSource(DMAMUX_INST, DMA_CH, 63);   /* always-on */
    DMAMUX_EnableChannel(DMAMUX_INST, DMA_CH);
    EDMA_GetDefaultConfig(&dcfg);
    EDMA_Init(DMA_INST, &dcfg);
    EDMA_CreateHandle(&gDma, DMA_INST, DMA_CH);

    while (1)
    {
        PRINTF("\r\n== ELF32 loader (eDMA-aware) ==\r\n");

        PRINTF("[1] SD init …\r\n");
        if (vcb_fs_init()) { PRINTF("    ❌ SD failure\r\n"); while (1); }
        PRINTF("    ✅ SD ready.\r\n");

        FIL f;
        if (f_open(&f, "/app.axf", FA_READ)) {
            PRINTF("    ❌ app.axf not found\r\n"); while (1);
        }

        uint8_t eh[0x34]; UINT br;
        f_read(&f, eh, sizeof(eh), &br);

        uint32_t phoff   = rd32le(&eh[0x1C]);
        uint16_t phentsz = eh[0x2A] | (eh[0x2B] << 8);
        uint16_t phnum   = eh[0x2C] | (eh[0x2D] << 8);
        PRINTF("    ↳ PH off 0x%X, size %u, count %u\r\n",
               phoff, phentsz, phnum);

        uint32_t firstAddr = 0xFFFFFFFFu;

        for (uint16_t i = 0; i < phnum; ++i)
        {
            f_lseek(&f, phoff + (uint32_t)i * phentsz);
            uint8_t ph[32]; f_read(&f, ph, sizeof(ph), &br);

            uint32_t type   = rd32le(&ph[0x00]);
            uint32_t offset = rd32le(&ph[0x04]);
            uint32_t paddr  = rd32le(&ph[0x08]);
            uint32_t filesz = rd32le(&ph[0x10]);
            uint32_t memsz  = rd32le(&ph[0x14]);

            if (type != 1u) continue;           /* PT_LOAD */

            if (firstAddr == 0xFFFFFFFFu && filesz)
                firstAddr = paddr;

            PRINTF("    • seg %u off 0x%X → 0x%08X sz 0x%X (mem 0x%X)%s\r\n",
                   i, offset, paddr, filesz, memsz,
                   dma_can_access((void *)paddr) ? "" : "  ⚠︎ CPU copy");

            uint32_t rem = filesz, pos = offset;
            uint8_t *dst = (uint8_t *)paddr;
            while (rem)
            {
                uint32_t chunk = (rem > SCRATCH_SZ) ? SCRATCH_SZ : rem;
                f_lseek(&f, pos); f_read(&f, scratch, chunk, &br);
                blk_copy(dst, scratch, chunk);
                dst += chunk; pos += chunk; rem -= chunk;
            }
            if (memsz > filesz)
                memset((void *)(paddr + filesz), 0, memsz - filesz);
        }
        f_close(&f);

        if (firstAddr == 0xFFFFFFFFu) {
            PRINTF("    ❌ no PT_LOAD segments\r\n"); while (1);
        }

        PRINTF("[✓] Jumping to 0x%08X …\r\n", firstAddr);
        call_app_and_return(firstAddr);
    }
}
