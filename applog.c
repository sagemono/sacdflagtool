#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <cell/cell_fs.h>

#include "applog.h"

static char     s_lines[LOG_MAX_LINES][LOG_MAX_COLS];
static int      s_head;
static int      s_count;
static int      s_dropped;

static const char *s_sink;
static char        s_sink_buf[128];
static int         s_last_errno;
static uint64_t    s_bytes;

static const char *s_sink_dirs[] = {
    "/dev_usb000",
    "/dev_usb001",
    "/dev_usb002",
    "/dev_usb003",
    "/dev_usb004",
    "/dev_usb005",
    "/dev_usb006",
    "/dev_usb007",
    "/dev_hdd0/game/SACDENABL/USRDIR",
    "/dev_hdd0/tmp",
    "/dev_hdd0",
};

#define SINK_DIR_COUNT ((int)(sizeof(s_sink_dirs) / sizeof(s_sink_dirs[0])))
#define LOG_FILE_NAME  "sacd_log.txt"

static const char *errno_name(int e)
{
    switch (e) {
    case 0:            return "OK";
    case ENOENT:       return "ENOENT (no such path)";
    case EACCES:       return "EACCES (permission denied)";
    case EPERM:        return "EPERM (not permitted)";
    case EROFS:        return "EROFS (read-only)";
    case ENOSPC:       return "ENOSPC (disk full)";
    case ENODEV:       return "ENODEV (no device)";
    case ENOTDIR:      return "ENOTDIR";
    case EISDIR:       return "EISDIR";
    case EBUSY:        return "EBUSY";
    case EIO:          return "EIO";
    case EMFILE:       return "EMFILE (too many open files)";
    case ENAMETOOLONG: return "ENAMETOOLONG";
    default:           return "error";
    }
}

void log_init(void)
{
    s_head    = 0;
    s_count   = 0;
    s_dropped = 0;
    s_sink    = NULL;
    s_bytes   = 0;
    s_last_errno = 0;
}

void log_line(const char *s)
{
    int slot;

    if (s_count < LOG_MAX_LINES) {
        slot = (s_head + s_count) % LOG_MAX_LINES;
        s_count++;
    } else {
        slot   = s_head;
        s_head = (s_head + 1) % LOG_MAX_LINES;
        s_dropped++;
    }

    strncpy(s_lines[slot], s, LOG_MAX_COLS - 1);
    s_lines[slot][LOG_MAX_COLS - 1] = 0;
    printf("%s\n", s_lines[slot]);
    fflush(stdout);
}

void log_fmt(const char *fmt, ...)
{
    char line[LOG_MAX_COLS];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    log_line(line);
}

void log_hex(const char *label, const uint8_t *b, int len)
{
    char line[LOG_MAX_COLS];
    int i, n = 0;

    n += snprintf(line + n, sizeof(line) - n, "%s", label);
    for (i = 0; i < len && n < (int)sizeof(line) - 4; i++)
        n += snprintf(line + n, sizeof(line) - n, "%02x ", b[i]);
    log_line(line);
}

void log_dump(const char *label, const uint8_t *b, int len)
{
    char line[LOG_MAX_COLS];
    int off, i, n;

    log_fmt("%s (%d bytes)", label, len);
    for (off = 0; off < len; off += 16) {
        n = snprintf(line, sizeof(line), "  %04x  ", off);
        for (i = 0; i < 16; i++)
            n += (off + i < len) ? snprintf(line + n, sizeof(line) - n, "%02x ", b[off + i]) : snprintf(line + n, sizeof(line) - n, "   ");
        n += snprintf(line + n, sizeof(line) - n, " |");
        for (i = 0; i < 16 && off + i < len; i++) {
            uint8_t c = b[off + i];
            n += snprintf(line + n, sizeof(line) - n, "%c", (c >= 0x20 && c < 0x7f) ? (char)c : '.');
        }
        snprintf(line + n, sizeof(line) - n, "|");
        log_line(line);
    }
}

int log_count(void)
{
    return s_count;
}

const char *log_at(int idx)
{
    if (idx < 0 || idx >= s_count)
        return "";
    return s_lines[(s_head + idx) % LOG_MAX_LINES];
}

static int sink_write(const char *path, const char *text, uint64_t len, int *out_err)
{
    CellFsStat st;
    uint64_t nwrite = 0;
    int fd = -1;
    int err;

    err = cellFsOpen(path, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &fd, NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        *out_err = err;
        return -1;
    }

    err = cellFsWrite(fd, text, len, &nwrite);
    if (err == CELL_FS_SUCCEEDED)
        cellFsFsync(fd);
    cellFsClose(fd);

    if (err != CELL_FS_SUCCEEDED) {
        *out_err = err;
        return -1;
    }
    if (nwrite != len) {
        *out_err = ENOSPC;
        return -1;
    }

    cellFsChmod(path, CELL_FS_S_IRWXU | CELL_FS_S_IRWXO);

    if (cellFsStat(path, &st) != CELL_FS_SUCCEEDED || st.st_size < len) {
        *out_err = EIO;
        return -1;
    }

    *out_err = 0;
    return 0;
}

static int s_probe_idx = -1;

static void log_probe_restart(void)
{
    s_probe_idx = 0;
    s_sink = NULL;
    log_line("log: looking for a writable location");
}

int log_probe_step(void)
{
    static const char probe_text[] = "sacd tool log\n";
    CellFsStat st;
    char path[128];
    const char *dir;
    int err;

    if (s_probe_idx < 0)
        log_probe_restart();

    if (s_probe_idx >= SINK_DIR_COUNT) {
        log_line("log: nothing is writable - the run stays on screen only");
        s_probe_idx = -1;
        return 1;
    }

    dir = s_sink_dirs[s_probe_idx++];

    log_fmt("  trying %s", dir);

    if (cellFsStat(dir, &st) != CELL_FS_SUCCEEDED) {
        log_fmt("  %-34s not mounted", dir);
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s", dir, LOG_FILE_NAME);
    if (sink_write(path, probe_text, sizeof(probe_text) - 1, &err) == 0) {
        strncpy(s_sink_buf, path, sizeof(s_sink_buf) - 1);
        s_sink_buf[sizeof(s_sink_buf) - 1] = 0;
        s_sink = s_sink_buf;
        s_last_errno = 0;
        s_probe_idx  = -1;
        log_fmt("  %-34s OK  <- using this", dir);
        return 1;
    }

    s_last_errno = err;
    log_fmt("  %-34s %s (0x%08x)", dir, errno_name(err), (unsigned)err);
    return 0;
}

void log_probe_sinks(void)
{
    log_probe_restart();
    while (!log_probe_step());
}

int log_flush(void)
{
    uint64_t nwrite = 0;
    int fd = -1;
    int i, err;

    if (!s_sink)
        log_probe_sinks();
    if (!s_sink)
        return -1;

    err = cellFsOpen(s_sink, CELL_FS_O_WRONLY | CELL_FS_O_CREAT | CELL_FS_O_TRUNC, &fd, NULL, 0);
    if (err != CELL_FS_SUCCEEDED) {
        s_last_errno = err;
        s_sink = NULL;
        return -1;
    }

    s_bytes = 0;
    if (s_dropped > 0) {
        char hdr[80];
        int n = snprintf(hdr, sizeof(hdr), "[%d earlier lines dropped]\n", s_dropped);
        if (cellFsWrite(fd, hdr, n, &nwrite) == CELL_FS_SUCCEEDED)
            s_bytes += nwrite;
    }

    for (i = 0; i < s_count; i++) {
        const char *line = log_at(i);
        size_t len = strlen(line);

        if (cellFsWrite(fd, line, len, &nwrite) != CELL_FS_SUCCEEDED)
            break;
        s_bytes += nwrite;
        if (cellFsWrite(fd, "\n", 1, &nwrite) != CELL_FS_SUCCEEDED)
            break;
        s_bytes += nwrite;
    }

    cellFsFsync(fd);
    cellFsClose(fd);
    cellFsChmod(s_sink, CELL_FS_S_IRWXU | CELL_FS_S_IRWXO);

    s_last_errno = 0;
    return 0;
}

const char *log_sink_path(void)
{
    return s_sink;
}