#include <stdint.h>

#include "lv2patch.h"

#define SIG_LEN   17
#define SIG_CORE  11

static const uint32_t sig_val[SIG_LEN] = {
    0xF821FF71, 0x7C0802A6, 0x90610070, 0xF80100A0,
    0x38610070, 0xE8820000, 0x38000002, 0x38A10080,
    0x38C10078, 0xF8010078, 0xF8010080, 0x48000001,
    0xE80100A0, 0x78630620, 0x7C0803A6, 0x38210090,
    0x4E800020
};

static const uint32_t sig_mask[SIG_LEN] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFF0000, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFC000003,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF
};

#define PATCH_W0  0x38600001u   // li r3,1
#define PATCH_W1  0x4E800020u   // blr

static int      s_backend = LV2_BACKEND_SC6;
static uint64_t s_lv1_off;

static uint64_t peek_sc6(uint64_t addr)
{
    return (uint64_t)lv2_syscall8(6, addr, 0, 0, 0, 0, 0, 0, 0);
}

static void poke_sc7(uint64_t addr, uint64_t value)
{
    lv2_syscall8(7, addr, value, 0, 0, 0, 0, 0, 0);
}

static uint64_t peek_lv1(uint64_t addr)
{
    return (uint64_t)lv2_syscall8(8, (addr - LV2_BASE) + s_lv1_off, 0, 0, 0, 0, 0, 0, 0);
}

static void poke_lv1(uint64_t addr, uint64_t value)
{
    lv2_syscall8(9, (addr - LV2_BASE) + s_lv1_off, value, 0, 0, 0, 0, 0, 0);
}

static int looks_like_kernel_ptr(uint64_t v)
{
    return (v & 0xFFFFFFFFFF000000ULL) == LV2_BASE;
}

int lv2_backend_detect(void)
{
    s_backend = LV2_BACKEND_SC6;
    if (looks_like_kernel_ptr(peek_sc6(LV2_TOC_ADDR)))
        return s_backend;

    s_backend = LV2_BACKEND_LV1;
    s_lv1_off = LV2_ON_LV1;
    if (looks_like_kernel_ptr(peek_lv1(LV2_TOC_ADDR)))
        return s_backend;

    s_lv1_off = 0;
    s_backend = LV2_BACKEND_NONE;
    return s_backend;
}

const char *lv2_backend_name(void)
{
    switch (s_backend) {
    case LV2_BACKEND_SC6: return "syscall 6/7 (lv2 peek/poke)";
    case LV2_BACKEND_LV1: return "syscall 8/9 (lv1 peek/poke)";
    default:              return "none, cannot reach lv2!";
    }
}

uint64_t lv2_peek(uint64_t addr)
{
    return (s_backend == LV2_BACKEND_LV1) ? peek_lv1(addr) : peek_sc6(addr);
}

void lv2_poke(uint64_t addr, uint64_t value)
{
    if (s_backend == LV2_BACKEND_LV1)
        poke_lv1(addr, value);
    else
        poke_sc7(addr, value);
}

uint32_t lv2_read_word(uint64_t addr)
{
    uint64_t q = lv2_peek(addr & ~7ULL);
    return (addr & 4) ? (uint32_t)q : (uint32_t)(q >> 32);
}

void lv2_write_word(uint64_t addr, uint32_t word)
{
    uint64_t aligned = addr & ~7ULL;
    uint64_t q = lv2_peek(aligned);

    if (addr & 4)
        q = (q & 0xFFFFFFFF00000000ULL) | (uint64_t)word;
    else
        q = (q & 0x00000000FFFFFFFFULL) | ((uint64_t)word << 32);

    lv2_poke(aligned, q);
}

static int match_words(uint64_t addr, int words)
{
    int i;

    for (i = 2; i < words; i++) {
        uint32_t w = lv2_read_word(addr + (uint64_t)i * 4);
        if ((w & sig_mask[i]) != sig_val[i])
            return 0;
    }
    return 1;
}

int lv2_verify(uint64_t addr)
{
    if (lv2_read_word(addr) != sig_val[0] ||
        lv2_read_word(addr + 4) != sig_val[1])
        return 0;
    if (match_words(addr, SIG_LEN))
        return 2;
    if (match_words(addr, SIG_CORE))
        return 1;
    return 0;
}

int lv2_is_patched(uint64_t addr)
{
    return lv2_read_word(addr) == PATCH_W0 && lv2_read_word(addr + 4) == PATCH_W1;
}

static void record_hit(lv2_scan_t *s, uint64_t addr, int strict)
{
    if (s->hit_count >= LV2_MAX_HITS) {
        s->overflow++;
        return;
    }
    s->hits[s->hit_count].addr   = addr;
    s->hits[s->hit_count].strict = strict;
    s->hit_count++;
}

void lv2_scan_begin(lv2_scan_t *s)
{
    int i;

    for (i = 0; i < LV2_MAX_HITS; i++) {
        s->hits[i].addr   = 0;
        s->hits[i].strict = 0;
    }
    s->hit_count = 0;
    s->overflow  = 0;
    s->pos       = LV2_SCAN_START;
    s->end       = LV2_SCAN_BYTES;
    s->prev_word = 0;
    s->peeks     = 0;
    s->done      = 0;
}

static void test_candidate(lv2_scan_t *s, uint64_t addr)
{
    int rank;

    if (lv2_is_patched(addr)) // tools like sacd-ripper patch this offset too, check if this is already patched and bail
        return;

    rank = lv2_verify(addr);
    if (rank > 0)
        record_hit(s, addr, rank == 2);
}

int lv2_scan_step(lv2_scan_t *s, uint32_t budget_bytes)
{
    uint32_t spent = 0;

    while (!s->done && spent < budget_bytes) {
        uint64_t q  = lv2_peek(LV2_BASE + s->pos);
        uint32_t w0 = (uint32_t)(q >> 32);
        uint32_t w1 = (uint32_t)q;

        s->peeks++;

        if (s->pos > LV2_SCAN_START && s->prev_word == sig_val[0] && w0 == sig_val[1])
            test_candidate(s, LV2_BASE + s->pos - 4);
        if (w0 == sig_val[0] && w1 == sig_val[1])
            test_candidate(s, LV2_BASE + s->pos);

        s->prev_word = w1;
        s->pos      += 8;
        spent       += 8;

        if (s->pos >= s->end)
            s->done = 1;
    }

    return s->done;
}

int lv2_scan_best(const lv2_scan_t *s)
{
    int i, found = -1, strict_count = 0;

    for (i = 0; i < s->hit_count; i++) {
        if (s->hits[i].strict) {
            strict_count++;
            found = i;
        }
    }

    return (strict_count == 1) ? found : -1;
}

int lv2_patch_apply(lv2_patch_t *p, uint64_t addr)
{
    p->addr    = addr;
    p->orig[0] = lv2_read_word(addr);
    p->orig[1] = lv2_read_word(addr + 4);
    p->applied = 0;

    lv2_write_word(addr, PATCH_W0);
    lv2_write_word(addr + 4, PATCH_W1);

    if (!lv2_is_patched(addr)) {
        lv2_write_word(addr, p->orig[0]);
        lv2_write_word(addr + 4, p->orig[1]);
        return -1;
    }

    p->applied = 1;
    return 0;
}

int lv2_patch_restore(lv2_patch_t *p)
{
    if (!p->applied)
        return 0;

    lv2_write_word(p->addr, p->orig[0]);
    lv2_write_word(p->addr + 4, p->orig[1]);

    if (lv2_read_word(p->addr) != p->orig[0] ||
        lv2_read_word(p->addr + 4) != p->orig[1])
        return -1;

    p->applied = 0;
    return 0;
}