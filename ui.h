#ifndef UI_H
#define UI_H

#include <stdint.h>

#define UI_CONS_W      90
#define UI_CONS_H      40
#define UI_TEXT_SCALE  0.50f
#define UI_BODY_X      0.03f
#define UI_BODY_Y      0.12f

#define UI_COL_TEXT    0xffd8d8d8
#define UI_COL_TITLE   0xff60d0ff
#define UI_COL_DIM     0xff808080

void ui_title(const char *left, const char *right);
void ui_footer(const char *hints);

void ui_body_begin(void);
void ui_line(const char *fmt, ...);
void ui_rule(void);

void ui_bar(char *buf, int buf_size, int width, uint32_t cur, uint32_t total);

#endif