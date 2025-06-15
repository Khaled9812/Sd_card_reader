#ifndef VCB_FS_H_
#define VCB_FS_H_

#include <stdint.h>
#include "ff.h"  // For FIL, UINT, etc.

/* ───── tiny “file” descriptor used by vcb_fs_read_path ───── */
typedef struct
{
    uint8_t  *ptr;     /* malloc’d buffer holding the whole file   */
    uint32_t  size;    /* length in bytes                          */
} VCB_FILE;

/* ───── Core helpers ───── */
int  vcb_fs_init(void);                                  /* mount µSD         */
int  vcb_fs_read_path(const char *path, VCB_FILE *file); /* read entire file  */

/* ───── ELF streaming helpers ───── */
int  vcb_fs_stream_to_addr(const char *path,
                           uint32_t    dest_addr,
                           uint32_t   *copied_bytes);    /* stream file to addr */

int  vcb_fs_stream_segment(FIL *f,
                           uint32_t offset,
                           uint32_t dest_addr,
                           uint32_t length);             /* stream partial */

int  vcb_fs_zero_fill(uint32_t dest_addr,
                      uint32_t length);                  /* fill RAM with 0s */

uint32_t vcb_fs_read_u32_le(const uint8_t *buf);         /* decode little-endian u32 */

/* ───── Jump to loaded app ───── */
__attribute__((noreturn))
void vcb_fs_jump(uint32_t app_base);                     /* sets VTOR + MSP + jumps */

#endif /* VCB_FS_H_ */
