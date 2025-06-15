#ifndef VCB_FS_H_
#define VCB_FS_H_

#include <stdint.h>

/* ───── tiny “file” descriptor used by vcb_fs_read_path ───── */
typedef struct
{
    uint8_t  *ptr;     /* malloc’d buffer holding the whole file   */
    uint32_t  size;    /* length in bytes                          */
} VCB_FILE;

/* ------------------------------------------------------------------ */
/* core helpers – already present                                     */
int vcb_fs_init      (void);                                /* 0 / -1 */
int vcb_fs_read_path (const char *path, VCB_FILE *file);    /* 0 / -1 */

/* ------------------------------------------------------------------ */
/* new helpers                                                        */
int  vcb_fs_stream_to_addr(const char *path,
                           uint32_t    dest_addr,
                           uint32_t   *copied_bytes);       /* 0 / -1 */

/* NOTE: vcb_fs_jump never returns – mark ‘noreturn’ for the compiler */
__attribute__((noreturn))
void vcb_fs_jump(uint32_t app_base);

#endif /* VCB_FS_H_ */
