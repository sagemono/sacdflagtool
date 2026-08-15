#ifndef GCM_H
#define GCM_H

#include <stdint.h>
#include <cell/gcm.h>
#include <cell/dbgfont.h>

extern uint32_t screen_width;
extern uint32_t screen_height;
extern void    *local_heap_ptr;

extern CellDbgFontConsoleId dbg_console;

int  init_display(void);
int  init_dbgfont(void *local_addr, uint32_t *local_used);
void set_render_target(void);
void flip_frame(void);

#endif