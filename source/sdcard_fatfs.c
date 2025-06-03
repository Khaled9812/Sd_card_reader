#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "vcb_fs.h"
#include "fsl_debug_console.h"
#include "ff.h"

extern void BOARD_ConfigMPU(void);
extern void BOARD_InitBootPins(void);
extern void BOARD_BootClockRUN(void);
extern void BOARD_InitDebugConsole(void);

int main(void)
{
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();

    PRINTF("\r\n== ELF App Loader ==\r\n");

    PRINTF("[1] Initialising SD card …\r\n");
    if (vcb_fs_init() != 0)
    {
        PRINTF("    ❌ SD card init failed – abort.\r\n");
        for (;;);
    }
    PRINTF("    ✅ SD card ready.\r\n");

    PRINTF("[2] Reading ELF file (app.axf) header …\r\n");
    FIL file;
    if (f_open(&file, "/app.axf", FA_READ))
    {
        PRINTF("    ❌ app.axf not found – abort.\r\n");
        for (;;);
    }

    uint8_t header[64];
    UINT br;
    if (f_read(&file, header, sizeof(header), &br) || br < sizeof(header))
    {
        PRINTF("    ❌ Failed to read ELF header – abort.\r\n");
        f_close(&file);
        for (;;);
    }

    uint32_t ph_offset = *(uint32_t *)&header[0x1C];
    uint16_t ph_entry_size = *(uint16_t *)&header[0x2A];
    uint16_t ph_count = *(uint16_t *)&header[0x2C];

    PRINTF("    ✅ Program Header Offset = 0x%08X\r\n", ph_offset);
    PRINTF("    ✅ Program Header Entry Size = %u\r\n", ph_entry_size);
    PRINTF("    ✅ Number of Program Headers = %u\r\n", ph_count);

    f_close(&file);

    for (;;);
}
