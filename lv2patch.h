#ifndef LV2PATCH_H
#define LV2PATCH_H

#include <stdint.h>
#include <sys/syscall.h>

static inline int64_t lv2_syscall8(uint64_t num,
    uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
    uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8)
{
    system_call_8(num, a1, a2, a3, a4, a5, a6, a7, a8);
    return (int64_t)p1;
}

#define LV2_BASE        0x8000000000000000ULL
#define LV2_SCAN_START  0x10000u
#define LV2_SCAN_BYTES  0x200000u
#define LV2_SCAN_SLICE  0x8000u
#define LV2_MAX_HITS    6

typedef struct {
    uint64_t addr;
    int      strict;
} lv2_hit_t;

typedef struct {
    lv2_hit_t hits[LV2_MAX_HITS];
    int       hit_count;
    int       overflow;
    uint32_t  pos;
    uint32_t  end;
    uint32_t  prev_word;
    uint32_t  peeks;
    int       done;
} lv2_scan_t;

typedef struct {
    uint64_t addr;
    uint32_t orig[2];
    int      applied;
} lv2_patch_t;

#define LV2_TOC_ADDR       0x8000000000003000ULL
#define LV2_ON_LV1         0x01000000ULL

enum { LV2_BACKEND_NONE = 0, LV2_BACKEND_SC6, LV2_BACKEND_LV1 };

int         lv2_backend_detect(void);
const char *lv2_backend_name(void);

uint64_t lv2_peek(uint64_t addr);
void     lv2_poke(uint64_t addr, uint64_t value);
uint32_t lv2_read_word(uint64_t addr);
void     lv2_write_word(uint64_t addr, uint32_t word);
void lv2_scan_begin(lv2_scan_t *s);
int  lv2_scan_step(lv2_scan_t *s, uint32_t budget_bytes);
int  lv2_scan_best(const lv2_scan_t *s);
int  lv2_verify(uint64_t addr);
int  lv2_is_patched(uint64_t addr);
int  lv2_patch_apply(lv2_patch_t *p, uint64_t addr);
int  lv2_patch_restore(lv2_patch_t *p);

#endif