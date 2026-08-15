#include <stdio.h>
#include <string.h>

#include <sys/process.h>
#include <sys/time_util.h>
#include <sys/sys_time.h>
#include <cell/sysmodule.h>
#include <cell/pad.h>
#include <sysutil/sysutil_sysparam.h>

#include "gcm.h"
#include "ui.h"
#include "applog.h"
#include "lv2patch.h"
#include "sacd.h"

SYS_PROCESS_PARAM(1001, 0x100000)

extern int sys_timer_usleep(uint64_t usec);

enum { VIEW_MENU = 0, VIEW_SCAN, VIEW_PICK, VIEW_RUN };

enum {
    MENU_SCAN = 0,
    MENU_READ,
    MENU_ENABLE,
    MENU_DISABLE,
    MENU_PATCHMODE,
    MENU_FLUSH,
    MENU_EXIT,
    MENU_COUNT
};

enum {
    OPS_PATCH = 0,
    OPS_OPEN,
    OPS_INQUIRY,
    OPS_PROFILE_BEFORE,
    OPS_D7_BEFORE,
    OPS_WRITE,
    OPS_D7_AFTER,
    OPS_PROFILE_AFTER,
    OPS_CLOSE,
    OPS_RESTORE,
    OPS_FLUSH,
    OPS_END
};

static const char *menu_text[MENU_COUNT] = {
    "Scan lv2 for the access check",
    "Read drive state",
    "Enable SACD",
    "Disable SACD",
    NULL,
    "Write log to disk now",
    "Exit"
};

static const char *op_text[OPS_END] = {
    "patch access check", "open drive", "inquiry", "profile before",
    "d7 read before", "d7 write", "d7 read after", "profile after",
    "close drive", "restore access check", "write log"
};

static volatile int app_running = 1;

static int s_view     = VIEW_MENU;
static int s_menu_sel = MENU_SCAN;
static int s_pick_sel;
static int s_scroll;
static int s_sinks_done;

static uint64_t s_tb_freq = 79800000ULL;
static uint64_t s_scan_tb0, s_scan_tb1;
static uint64_t s_heartbeat_tb;

static lv2_scan_t  s_scan;
static lv2_patch_t s_patch;
static uint64_t s_target;
static int      s_target_valid;
static int      s_target_strict;
static int      s_peek_ok;
static int      s_use_patch = 1;

static int      s_op_step;
static int      s_op_write;
static int      s_op_fd = -1;
static uint8_t  s_flag_before, s_flag_after;
static int      s_flag_before_ok, s_flag_after_ok;
static uint32_t s_profile_before, s_profile_after;
static char     s_drive_name[36];

static void sysutil_callback(uint64_t status, uint64_t param, void *userdata)
{
    (void)param;
    (void)userdata;
    if (status == CELL_SYSUTIL_REQUEST_EXITGAME)
        app_running = 0;
}

static double scan_elapsed(void)
{
    uint64_t now;

    if (s_scan_tb1 > s_scan_tb0)
        now = s_scan_tb1;
    else
        SYS_TIMEBASE_GET(now);
    return (double)(now - s_scan_tb0) / (double)s_tb_freq;
}

static void scan_finished(void)
{
    int best, i;

    log_fmt("scan done: %u peeks, %d candidate%s%s", (unsigned)s_scan.peeks, s_scan.hit_count, s_scan.hit_count == 1 ? "" : "s", s_scan.overflow ? " (more were skipped)" : "");

    for (i = 0; i < s_scan.hit_count; i++)
        log_fmt("  [%d] 0x%016llx  %s", i, (unsigned long long)s_scan.hits[i].addr, s_scan.hits[i].strict ? "full match" : "partial match");

    best = lv2_scan_best(&s_scan);
    if (best >= 0) {
        s_target        = s_scan.hits[best].addr;
        s_target_valid  = 1;
        s_target_strict = 1;
        log_fmt("target: 0x%016llx", (unsigned long long)s_target);
        s_view     = VIEW_MENU;
        s_menu_sel = MENU_READ;
        return;
    }

    if (s_scan.hit_count == 0) {
        log_line("no match on this kernel, nothing was patched");
        s_view = VIEW_MENU;
        return;
    }

    log_line("no single full match, pick one by hand or back out");
    s_pick_sel = 0;
    s_view = VIEW_PICK;
}

static void scan_start(void)
{
    if (lv2_backend_detect() == LV2_BACKEND_NONE) {
        log_line("lv2 is not reachable, peek returned nothing that looks like");
        log_line("kernel memory on syscalls 6/7 or 8/9");
        return;
    }

    lv2_scan_begin(&s_scan);
    s_peek_ok      = 1;
    s_target_valid = 0;
    s_scan_tb1     = 0;
    SYS_TIMEBASE_GET(s_scan_tb0);
    s_heartbeat_tb = s_scan_tb0;
    s_view         = VIEW_SCAN;

    log_line("========================================");
    log_fmt("lv2 access via %s", lv2_backend_name());
    log_fmt("scanning lv2 0x%016llx .. 0x%016llx  (%u peeks)", (unsigned long long)(LV2_BASE + LV2_SCAN_START), (unsigned long long)(LV2_BASE + LV2_SCAN_BYTES), (unsigned)((LV2_SCAN_BYTES - LV2_SCAN_START) / 8));
}

static void scan_tick(void)
{
    uint64_t now;

    if (lv2_scan_step(&s_scan, LV2_SCAN_SLICE)) {
        double took;

        SYS_TIMEBASE_GET(s_scan_tb1);
        took = scan_elapsed();
        log_fmt("scan took %.2f s (%.0f peeks/s)", took, took > 0.0 ? (double)s_scan.peeks / took : 0.0);
        scan_finished();
        return;
    }

    SYS_TIMEBASE_GET(now);
    if (now - s_heartbeat_tb > s_tb_freq) {
        s_heartbeat_tb = now;
        printf("sacd: scan at 0x%016llx  %u peeks  %.1fs\n", (unsigned long long)(LV2_BASE + s_scan.pos), (unsigned)s_scan.peeks, scan_elapsed());
        fflush(stdout);
    }
}

static void op_start(int write_flag)
{
    if (s_use_patch && !s_target_valid) {
        log_line("no access check located yet, scan first, or turn the patch OFF");
        return;
    }

    s_op_step        = OPS_PATCH;
    s_op_write       = write_flag;
    s_op_fd          = -1;
    s_flag_before_ok = 0;
    s_flag_after_ok  = 0;
    s_profile_before = 0;
    s_profile_after  = 0;
    s_drive_name[0]  = 0;
    s_view           = VIEW_RUN;

    log_line("========================================");
    if (write_flag < 0)
        log_line("--- run: read only, no write ---");
    else
        log_fmt("--- run: writing 0x%02x (%s) ---", (unsigned)write_flag, write_flag == SACD_ENABLE ? "ENABLE" : "DISABLE");
}

static void op_abort(const char *why)
{
    log_fmt("aborting: %s", why);
    if (s_op_fd >= 0) {
        sacd_close(s_op_fd);
        s_op_fd = -1;
    }
    s_op_step = OPS_RESTORE;
}

static void op_step(void)
{
    int ret;

    switch (s_op_step) {
    case OPS_PATCH:
        if (!s_use_patch) {
            log_line("lv2 patch OFF, opening the drive without touching the kernel");
            s_patch.applied = 0;
        } else if (lv2_is_patched(s_target)) {
            log_line("access check already patched, leaving it alone");
            s_patch.applied = 0;
        } else if (lv2_verify(s_target) == 0) {
            op_abort("signature no longer matches at the target address");
            return;
        } else if (lv2_patch_apply(&s_patch, s_target) != 0) {
            op_abort("poke did not stick");
            return;
        } else {
            log_fmt("patched 0x%016llx  %08x %08x -> 38600001 4e800020", (unsigned long long)s_target, s_patch.orig[0], s_patch.orig[1]);
        }
        break;

    case OPS_OPEN:
        ret = sacd_open(&s_op_fd);
        if (ret) {
            log_fmt("sys_storage_open failed: 0x%08x", (unsigned)ret);
            s_op_fd = -1;
            op_abort("cannot open the drive");
            return;
        }
        log_line("drive opened");
        break;

    case OPS_INQUIRY:
        ret = sacd_inquiry(s_op_fd);
        if (ret == 0) {
            memcpy(s_drive_name, sacd_iobuf() + 8, 28);
            s_drive_name[28] = 0;
            log_fmt("inquiry: %s", s_drive_name);
            log_dump("inquiry raw", sacd_iobuf(), 0x3c);
        } else {
            log_fmt("inquiry failed: 0x%08x", (unsigned)ret);
        }
        break;

    case OPS_PROFILE_BEFORE:
        ret = sacd_profile(s_op_fd, &s_profile_before);
        if (ret == 0)
            log_fmt("profile before: 0x%05x  %s", s_profile_before, sacd_profile_name(s_profile_before));
        else
            log_fmt("profile before failed: 0x%08x", (unsigned)ret);
        break;

    case OPS_D7_BEFORE:
        ret = sacd_d7_get(s_op_fd, &s_flag_before);
        if (ret == 0) {
            s_flag_before_ok = 1;
            log_fmt("d7 flag byte[11] BEFORE: 0x%02x   (0xff=on 0x53=off)", s_flag_before);
            log_dump("d7 response BEFORE", sacd_iobuf(), 0x40);
        } else {
            log_fmt("d7 get failed: 0x%08x", (unsigned)ret);
        }
        log_flush();
        break;

    case OPS_WRITE:
        if (s_op_write < 0) {
            log_line("read only, nothing written");
            break;
        }
        if (s_flag_before_ok && s_flag_before == (uint8_t)s_op_write)
            log_fmt("flag is already 0x%02x, writing it again anyway", (unsigned)s_op_write);
        log_fmt("writing d7 flag 0x%02x ...", (unsigned)s_op_write);
        ret = sacd_d7_set(s_op_fd, (uint8_t)s_op_write);
        log_line(ret ? "d7 set failed" : "d7 set returned ok");
        log_flush();
        break;

    case OPS_D7_AFTER:
        if (s_op_write < 0)
            break;
        ret = sacd_d7_get(s_op_fd, &s_flag_after);
        if (ret == 0) {
            s_flag_after_ok = 1;
            log_fmt("d7 flag byte[11] AFTER: 0x%02x", s_flag_after);
            log_dump("d7 response AFTER", sacd_iobuf(), 0x40);
            if (s_flag_after != (uint8_t)s_op_write)
                log_fmt("WARNING: readback 0x%02x != written 0x%02x, not accepted", s_flag_after, (unsigned)s_op_write);
            else
                log_line("readback matches, drive accepted it");
        } else {
            log_fmt("d7 get failed: 0x%08x", (unsigned)ret);
        }
        break;

    case OPS_PROFILE_AFTER:
        ret = sacd_profile(s_op_fd, &s_profile_after);
        if (ret == 0) {
            log_fmt("profile after: 0x%05x  %s", s_profile_after, sacd_profile_name(s_profile_after));
            log_hex("profile raw:   ", sacd_iobuf(), 8);
        } else {
            log_fmt("profile after failed: 0x%08x", (unsigned)ret);
        }
        break;

    case OPS_CLOSE:
        if (s_op_fd >= 0) {
            sacd_close(s_op_fd);
            s_op_fd = -1;
            log_line("drive closed");
        }
        break;

    case OPS_RESTORE:
        if (s_patch.applied)
            log_line(lv2_patch_restore(&s_patch) == 0 ? "access check restored" : "WARNING: could not restore the access check");
        break;

    case OPS_FLUSH:
        if (log_flush() == 0)
            log_fmt("log written to %s", log_sink_path());
        else
            log_line("log stayed in memory - no writable location");
        log_line("--- done ---");
        break;

    default:
        break;
    }

    s_op_step++;
    if (s_op_step >= OPS_END)
        s_view = VIEW_MENU;
}

static void draw_log_tail(int rows)
{
    int total = log_count();
    int first, i;

    if (rows <= 0)
        return;

    first = total - rows - s_scroll;
    if (first < 0)
        first = 0;

    for (i = 0; i < rows && first + i < total; i++)
        ui_line("%s", log_at(first + i));
}

static void draw_status(void)
{
    ui_line(" lv2 access  %s", s_peek_ok ? lv2_backend_name() : "not tried yet");

    if (!s_use_patch)
        ui_line(" target      not needed, lv2 patch is OFF");
    else if (s_target_valid)
        ui_line(" target      0x%016llx  (%s)%s", (unsigned long long)s_target, s_target_strict ? "full signature" : "partial signature", (s_peek_ok && lv2_is_patched(s_target)) ? "  [PATCHED NOW]" : "");
    else
        ui_line(" target      not located yet, run the scan");

    ui_line(" log file    %s", log_sink_path() ? log_sink_path() : "none writable (screen only)");

    if (s_drive_name[0])
        ui_line(" drive       %s", s_drive_name);

    if (s_flag_before_ok && s_flag_after_ok)
        ui_line(" sacd flag   before 0x%02x   after 0x%02x", s_flag_before, s_flag_after);
    else if (s_flag_before_ok)
        ui_line(" sacd flag   before 0x%02x", s_flag_before);
}

static void draw_menu(void)
{
    int i;

    draw_status();
    ui_rule();
    for (i = 0; i < MENU_COUNT; i++) {
        const char *cursor = (i == s_menu_sel) ? ">" : " ";
        if (i == MENU_PATCHMODE)
            ui_line(" %s lv2 patch: %s", cursor, s_use_patch ? "ON  (patch the access check)" : "OFF (no lv2 access at all)");
        else
            ui_line(" %s %s", cursor, menu_text[i]);
    }
    ui_rule();
    draw_log_tail(UI_CONS_H - MENU_COUNT - 10);
}

static void draw_scan(void)
{
    char bar[64];
    double elapsed = scan_elapsed();
    double rate    = (elapsed > 0.05) ? (double)s_scan.peeks / elapsed : 0.0;
    uint32_t total = (s_scan.end - LV2_SCAN_START) / 8;

    ui_line("");
    ui_bar(bar, sizeof(bar), 46, s_scan.pos, s_scan.end);
    ui_line("  scanning lv2   %s", bar);
    ui_line("  at 0x%016llx    %u / %u peeks    %d candidate(s)", (unsigned long long)(LV2_BASE + s_scan.pos), (unsigned)s_scan.peeks, (unsigned)total, s_scan.hit_count);

    if (rate > 0.0) {
        double left = (double)total - (double)s_scan.peeks;
        ui_line("  %.1f s elapsed    %.0f peeks/s    about %.0f s left", elapsed, rate, left > 0.0 ? left / rate : 0.0);
    } else {
        ui_line("  %.1f s elapsed", elapsed);
    }

    ui_rule();
    draw_log_tail(UI_CONS_H - 10);
}

static void draw_pick(void)
{
    int i;

    ui_line("");
    ui_line("  more than one candidate matched. the partial-match twin is a");
    ui_line("  different check on the same lookup - patching it will not help.");
    ui_line("");
    for (i = 0; i < s_scan.hit_count; i++)
        ui_line("  %s [%d] 0x%016llx   %s", (i == s_pick_sel) ? ">" : " ", i, (unsigned long long)s_scan.hits[i].addr, s_scan.hits[i].strict ? "full match" : "partial match");
    ui_rule();
    draw_log_tail(UI_CONS_H - s_scan.hit_count - 10);
}

static void draw_run(void)
{
    int i;

    ui_line("");
    for (i = 0; i < OPS_END; i++)
        ui_line("   %s  %s", (i < s_op_step) ? "done" : (i == s_op_step) ? " >> " : "    ", op_text[i]);
    ui_rule();
    draw_log_tail(UI_CONS_H - OPS_END - 6);
}

static void render(void)
{
    char status[80];
    const char *hints;

    snprintf(status, sizeof(status), "lv2 %s | log %s | %d lines", s_target_valid ? "located" : "unknown", log_sink_path() ? "on disk" : "memory", log_count());
    ui_title("SACD flag tool", status);

    ui_body_begin();
    switch (s_view) {
    case VIEW_SCAN: draw_scan(); break;
    case VIEW_PICK: draw_pick(); break;
    case VIEW_RUN:  draw_run();  break;
    default:        draw_menu(); break;
    }

    switch (s_view) {
    case VIEW_SCAN: hints = "O cancel   L1/R1 scroll log   SELECT+START quit"; break;
    case VIEW_PICK: hints = "UP/DOWN pick   X use this address   O back"; break;
    case VIEW_RUN:  hints = "O abort after this step   L1/R1 scroll log"; break;
    default:        hints = "UP/DOWN move   X select   /\\ write log   "
                            "L1/R1 scroll log   SELECT+START quit"; break;
    }
    ui_footer(hints);
}

static void menu_activate(void)
{
    switch (s_menu_sel) {
    case MENU_SCAN:    scan_start(); break;
    case MENU_READ:    op_start(-1); break;
    case MENU_ENABLE:  op_start(SACD_ENABLE); break;
    case MENU_DISABLE: op_start(SACD_DISABLE); break;
    case MENU_PATCHMODE:
        s_use_patch = !s_use_patch;
        log_fmt("lv2 patch %s", s_use_patch ? "ON" : "OFF");
        break;
    case MENU_FLUSH:
        if (log_flush() == 0)
            log_fmt("log written to %s", log_sink_path());
        else
            log_line("still nothing writable");
        break;
    case MENU_EXIT:    app_running = 0; break;
    default: break;
    }
}

static void handle_input(uint16_t pressed1, uint16_t pressed2)
{
    if (pressed2 & CELL_PAD_CTRL_L1)
        s_scroll += 8;
    if (pressed2 & CELL_PAD_CTRL_R1)
        s_scroll = (s_scroll > 8) ? s_scroll - 8 : 0;
    if (s_scroll > log_count())
        s_scroll = log_count();

    switch (s_view) {
    case VIEW_MENU:
        if (pressed1 & CELL_PAD_CTRL_UP)
            s_menu_sel = (s_menu_sel + MENU_COUNT - 1) % MENU_COUNT;
        if (pressed1 & CELL_PAD_CTRL_DOWN)
            s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
        if (pressed2 & CELL_PAD_CTRL_CROSS)
            menu_activate();
        if (pressed2 & CELL_PAD_CTRL_TRIANGLE)
            log_flush();
        break;

    case VIEW_SCAN:
        if (pressed2 & CELL_PAD_CTRL_CIRCLE) {
            log_line("scan cancelled");
            s_view = VIEW_MENU;
        }
        break;

    case VIEW_PICK:
        if (pressed1 & CELL_PAD_CTRL_UP)
            s_pick_sel = (s_pick_sel + s_scan.hit_count - 1) % s_scan.hit_count;
        if (pressed1 & CELL_PAD_CTRL_DOWN)
            s_pick_sel = (s_pick_sel + 1) % s_scan.hit_count;
        if (pressed2 & CELL_PAD_CTRL_CROSS) {
            s_target        = s_scan.hits[s_pick_sel].addr;
            s_target_valid  = 1;
            s_target_strict = s_scan.hits[s_pick_sel].strict;
            log_fmt("target: 0x%016llx (picked by hand)", (unsigned long long)s_target);
            s_view     = VIEW_MENU;
            s_menu_sel = MENU_READ;
        }
        if (pressed2 & CELL_PAD_CTRL_CIRCLE)
            s_view = VIEW_MENU;
        break;

    case VIEW_RUN:
        if (pressed2 & CELL_PAD_CTRL_CIRCLE)
            op_abort("cancelled from the pad");
        break;

    default:
        break;
    }
}

int main(void)
{
    uint32_t dbgfont_local_used = 0;
    uint16_t prev1 = 0, prev2 = 0;
    int ret;

    log_init();

    cellSysmoduleLoadModule(CELL_SYSMODULE_GCM_SYS);
    cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
    cellSysmoduleLoadModule(CELL_SYSMODULE_IO);
    cellSysutilRegisterCallback(0, sysutil_callback, NULL);

    ret = init_display();
    if (ret != 0) {
        printf("sacd: init_display failed: %d\n", ret);
        return 1;
    }
    printf("sacd: display %ux%u ok\n", screen_width, screen_height);

    ret = init_dbgfont(local_heap_ptr, &dbgfont_local_used);
    if (ret != 0) {
        printf("sacd: init_dbgfont failed: 0x%x\n", ret);
        return 1;
    }
    local_heap_ptr = (void *)((uintptr_t)local_heap_ptr + dbgfont_local_used);

    cellPadInit(1);

    if (sys_time_get_timebase_frequency() > 0)
        s_tb_freq = sys_time_get_timebase_frequency();

    log_line("sacd flag tool");
    log_fmt("screen %ux%u", screen_width, screen_height);
    log_line("select 'Scan lv2' to locate the access check on this firmware");

    while (app_running) {
        CellPadData pad_data;

        cellSysutilCheckCallback();

        if (cellPadGetData(0, &pad_data) == CELL_OK && pad_data.len > 0) {
            uint16_t b1 = pad_data.button[CELL_PAD_BTN_OFFSET_DIGITAL1];
            uint16_t b2 = pad_data.button[CELL_PAD_BTN_OFFSET_DIGITAL2];
            uint16_t p1 = b1 & ~prev1;
            uint16_t p2 = b2 & ~prev2;
            prev1 = b1;
            prev2 = b2;

            if ((b1 & (CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START)) ==
                (CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START))
                app_running = 0;

            handle_input(p1, p2);
        }

        if (s_view == VIEW_SCAN)
            scan_tick();
        else if (s_view == VIEW_RUN)
            op_step();

        {
            CellGcmContextData *ctx = gCellGcmCurrentContext;
            set_render_target();
            cellGcmSetClearColor(ctx, 0xff101010);
            cellGcmSetClearSurface(ctx, CELL_GCM_CLEAR_R | CELL_GCM_CLEAR_G | CELL_GCM_CLEAR_B | CELL_GCM_CLEAR_A);
        }

        render();
        cellDbgFontDrawGcm();
        flip_frame();

        if (!s_sinks_done && log_probe_step())
            s_sinks_done = 1;
    }

    if (s_op_fd >= 0)
        sacd_close(s_op_fd);
    if (s_patch.applied)
        lv2_patch_restore(&s_patch);
    log_line("--- exit ---");
    log_flush();

    cellPadEnd();
    cellDbgFontConsoleClose(dbg_console);
    cellDbgFontExitGcm();

    sys_timer_usleep(200000);
    sys_process_exit(0);
    return 0;
}