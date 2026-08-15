#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <cell/gcm.h>
#include <cell/dbgfont.h>
#include <sysutil/sysutil_sysparam.h>

#include "gcm.h"
#include "ui.h"

#define GCM_HOST_SIZE   (1 * 1024 * 1024)
#define GCM_CB_SIZE     (0x10000)
#define FRAME_COUNT     2

uint32_t screen_width;
uint32_t screen_height;
void    *local_heap_ptr;
CellDbgFontConsoleId dbg_console;

static uint32_t color_pitch;
static uint32_t color_offset[FRAME_COUNT];
static uint32_t depth_offset;
static uint32_t frame_index = 0;

int init_display(void)
{
    CellVideoOutState      video_state;
    CellVideoOutResolution video_res;
    CellVideoOutConfiguration vconfig;
    CellGcmConfig          gcm_config;
    void    *host_addr;
    void    *local_heap;
    uint32_t color_size;
    int i;

    cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &video_state);
    cellVideoOutGetResolution(video_state.displayMode.resolutionId, &video_res);
    screen_width  = video_res.width;
    screen_height = video_res.height;
    color_pitch   = screen_width * 4;

    host_addr = memalign(1024 * 1024, GCM_HOST_SIZE);
    if (!host_addr)
        return -1;
    if (cellGcmInit(GCM_CB_SIZE, GCM_HOST_SIZE, host_addr) != CELL_OK)
        return -1;

    cellGcmGetConfiguration(&gcm_config);
    local_heap = gcm_config.localAddress;

    color_size = color_pitch * screen_height;
    for (i = 0; i < FRAME_COUNT; i++) {
        cellGcmAddressToOffset(local_heap, &color_offset[i]);
        cellGcmSetDisplayBuffer(i, color_offset[i], color_pitch, screen_width, screen_height);
        local_heap = (void *)((uintptr_t)local_heap + color_size);
    }

    cellGcmAddressToOffset(local_heap, &depth_offset);
    local_heap = (void *)((uintptr_t)local_heap + color_size);

    local_heap_ptr = local_heap;

    memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolutionId = video_state.displayMode.resolutionId;
    vconfig.format       = CELL_VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8;
    vconfig.pitch        = color_pitch;
    cellVideoOutConfigure(CELL_VIDEO_OUT_PRIMARY, &vconfig, NULL, 0);

    cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);

    return 0;
}

void set_render_target(void)
{
    CellGcmContextData *ctx = gCellGcmCurrentContext;
    CellGcmSurface sf;
    memset(&sf, 0, sizeof(sf));

    sf.type             = CELL_GCM_SURFACE_PITCH;
    sf.antialias        = CELL_GCM_SURFACE_CENTER_1;
    sf.colorFormat      = CELL_GCM_SURFACE_A8R8G8B8;
    sf.colorTarget      = CELL_GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = CELL_GCM_LOCATION_LOCAL;
    sf.colorOffset[0]   = color_offset[frame_index];
    sf.colorPitch[0]    = color_pitch;
    sf.colorLocation[1] = CELL_GCM_LOCATION_LOCAL;
    sf.colorOffset[1]   = 0;
    sf.colorPitch[1]    = 64;
    sf.colorLocation[2] = CELL_GCM_LOCATION_LOCAL;
    sf.colorOffset[2]   = 0;
    sf.colorPitch[2]    = 64;
    sf.colorLocation[3] = CELL_GCM_LOCATION_LOCAL;
    sf.colorOffset[3]   = 0;
    sf.colorPitch[3]    = 64;
    sf.depthFormat      = CELL_GCM_SURFACE_Z24S8;
    sf.depthLocation    = CELL_GCM_LOCATION_LOCAL;
    sf.depthOffset      = depth_offset;
    sf.depthPitch       = color_pitch;
    sf.width            = screen_width;
    sf.height           = screen_height;
    sf.x                = 0;
    sf.y                = 0;

    cellGcmSetSurface(ctx, &sf);
}

int init_dbgfont(void *local_addr, uint32_t *local_used)
{
    CellDbgFontConfigGcm     cfg;
    CellDbgFontConsoleConfig con_cfg;
    int ret;

    uint32_t max_chars   = UI_CONS_W * (UI_CONS_H + 8);
    uint32_t frag_size   = CELL_DBGFONT_FRAGMENT_SIZE;
    uint32_t tex_size    = CELL_DBGFONT_TEXTURE_SIZE;
    uint32_t vertex_size = max_chars * CELL_DBGFONT_VERTEX_SIZE;
    uint32_t local_size  = frag_size + tex_size + vertex_size;

    memset(&cfg, 0, sizeof(cfg));
    cfg.localBufAddr = (uint32_t)(uintptr_t)local_addr;
    cfg.localBufSize = local_size;
    cfg.mainBufAddr  = 0;
    cfg.mainBufSize  = 0;
    cfg.screenWidth  = screen_width;
    cfg.screenHeight = screen_height;
    cfg.option       = CELL_DBGFONT_VERTEX_LOCAL | CELL_DBGFONT_TEXTURE_LOCAL | CELL_DBGFONT_SYNC_ON | CELL_DBGFONT_VIEWPORT_ON | CELL_DBGFONT_MINFILTER_LINEAR;

    ret = cellDbgFontInitGcm(&cfg);
    if (ret != CELL_OK) {
        printf("dbgfont: cellDbgFontInitGcm failed: 0x%x\n", ret);
        return ret;
    }

    *local_used = local_size;
    
    memset(&con_cfg, 0, sizeof(con_cfg));
    con_cfg.posLeft   = UI_BODY_X;
    con_cfg.posTop    = UI_BODY_Y;
    con_cfg.cnsWidth  = UI_CONS_W;
    con_cfg.cnsHeight = UI_CONS_H;
    con_cfg.scale     = UI_TEXT_SCALE;
    con_cfg.color     = UI_COL_TEXT;

    dbg_console = cellDbgFontConsoleOpen(&con_cfg);
    if (dbg_console < 0) {
        printf("dbgfont: consoleOpen failed: %d\n", (int)dbg_console);
        return dbg_console;
    }

    return 0;
}

void flip_frame(void)
{
    CellGcmContextData *ctx = gCellGcmCurrentContext;
    static int first_flip = 1;

    if (!first_flip)
        cellGcmSetWaitFlip(ctx);
    first_flip = 0;

    cellGcmSetFlip(ctx, frame_index);
    cellGcmFlush(ctx);
    cellGcmSetWaitFlip(ctx);

    frame_index ^= 1;
}
