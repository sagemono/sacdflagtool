#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <cell/dbgfont.h>

#include "gcm.h"
#include "ui.h"

void ui_title(const char *left, const char *right)
{
    cellDbgFontPuts(UI_BODY_X, 0.03f, 0.62f, UI_COL_TITLE, left);
    if (right) {
        cellDbgFontPuts(0.60f, 0.03f, 0.50f, UI_COL_DIM, right);
    }
    cellDbgFontPuts(UI_BODY_X, 0.075f, 0.50f, UI_COL_DIM, "------------------------------------------------------------------------------------------");
}

void ui_footer(const char *hints)
{
    cellDbgFontPuts(UI_BODY_X, 0.925f, 0.50f, UI_COL_DIM, "------------------------------------------------------------------------------------------");
    cellDbgFontPuts(UI_BODY_X, 0.955f, 0.50f, UI_COL_TEXT, hints);
}

void ui_body_begin(void)
{
    cellDbgFontConsoleClear(dbg_console);
}

void ui_line(const char *fmt, ...)
{
    char line[UI_CONS_W + 2];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    cellDbgFontConsolePrintf(dbg_console, "%s\n", line);
}

void ui_rule(void)
{
    ui_line("------------------------------------------------------------------------------");
}

void ui_bar(char *buf, int buf_size, int width, uint32_t cur, uint32_t total)
{
    int filled, i, n = 0;

    if (width > buf_size - 8)
        width = buf_size - 8;
    if (total == 0)
        total = 1;
    if (cur > total)
        cur = total;

    filled = (int)((uint64_t)cur * (uint64_t)width / (uint64_t)total);

    buf[n++] = '[';
    for (i = 0; i < width && n < buf_size - 7; i++)
        buf[n++] = (i < filled) ? '#' : '-';
    buf[n++] = ']';
    buf[n] = 0;
    snprintf(buf + n, buf_size - n, " %3u%%", (unsigned)((uint64_t)cur * 100ULL / (uint64_t)total));
}