#include <stdint.h>
#include <string.h>

#include "lv2patch.h"
#include "sacd.h"

#define ATAPI_PIO_DATA_IN  1
#define ATAPI_DIR_READ     1

struct lv2_atapi_cmnd_block {
    uint8_t  pkt[0x20];
    uint32_t pktlen;
    uint32_t blocks;
    uint32_t block_size;
    uint32_t proto;
    uint32_t in_out;
    uint32_t unknown;
} __attribute__((packed));

static uint8_t io_buf[0x1000] __attribute__((aligned(128)));

#define sc(n, a1, a2, a3, a4, a5, a6, a7, a8) \
    lv2_syscall8((n), (a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8))

static void init_cmd(struct lv2_atapi_cmnd_block *c, uint32_t block_size, uint32_t proto, uint32_t in_out)
{
    memset(c, 0, sizeof(*c));
    c->pktlen     = 12;
    c->blocks     = 1;
    c->block_size = block_size;
    c->proto      = proto;
    c->in_out     = in_out;
}

static int send_atapi(int fd, struct lv2_atapi_cmnd_block *cmd, uint8_t *buf)
{
    uint64_t tag;
    return (int)sc(604, (uint64_t)fd, 1, (uint64_t)(uintptr_t)cmd, sizeof(*cmd), (uint64_t)(uintptr_t)buf, cmd->block_size, (uint64_t)(uintptr_t)&tag, 0);
}

int sacd_open(int *fd)
{
    return (int)sc(600, BD_DEVICE, 0, (uint64_t)(uintptr_t)fd, 0, 0, 0, 0, 0);
}

int sacd_close(int fd)
{
    return (int)sc(601, (uint64_t)fd, 0, 0, 0, 0, 0, 0, 0);
}

int sacd_inquiry(int fd)
{
    struct lv2_atapi_cmnd_block cmd;

    memset(io_buf, 0, 0x80);
    init_cmd(&cmd, 0x3c, ATAPI_PIO_DATA_IN, ATAPI_DIR_READ);
    cmd.pkt[0] = 0x12;
    cmd.pkt[4] = 0x3c;

    return send_atapi(fd, &cmd, io_buf);
}

int sacd_profile(int fd, uint32_t *profile)
{
    struct lv2_atapi_cmnd_block cmd;
    int ret;

    memset(io_buf, 0, 64);
    init_cmd(&cmd, 4, ATAPI_PIO_DATA_IN, ATAPI_DIR_READ);
    cmd.pkt[0] = 0xfd;
    cmd.pkt[1] = 0x11;

    ret = send_atapi(fd, &cmd, io_buf);
    if (ret == 0 && profile)
        *profile = ((uint32_t)io_buf[0] << 24) | ((uint32_t)io_buf[1] << 16) | ((uint32_t)io_buf[2] << 8)  |  (uint32_t)io_buf[3];
    return ret;
}

static int sacd_d7(int fd, int set, uint8_t flag, uint8_t *out_flag)
{
    struct lv2_atapi_cmnd_block cmd;
    int ret;

    memset(io_buf, 0, 0x100);
    init_cmd(&cmd, 0x72, ATAPI_PIO_DATA_IN, ATAPI_DIR_READ);

    cmd.pkt[0]  = 0xd7;
    cmd.pkt[1]  = 0x1a;
    cmd.pkt[2]  = set ? 0x0e : 0x0f;
    cmd.pkt[3]  = 0x0f;
    cmd.pkt[6]  = 0x06;
    cmd.pkt[7]  = 0x72;
    cmd.pkt[11] = set ? flag : 0x00;

    ret = send_atapi(fd, &cmd, io_buf);
    if (ret == 0 && out_flag)
        *out_flag = io_buf[11];
    return ret;
}

int sacd_d7_get(int fd, uint8_t *flag)
{
    return sacd_d7(fd, 0, 0x00, flag);
}

int sacd_d7_set(int fd, uint8_t flag)
{
    return sacd_d7(fd, 1, flag, NULL);
}

const uint8_t *sacd_iobuf(void)
{
    return io_buf;
}

const char *sacd_profile_name(uint32_t p)
{
    switch (p) {
    case 0x00000: return "No Current Profile";
    case 0x00008: return "CD-ROM";
    case 0x00009: return "CD-R";
    case 0x0000a: return "CD-RW";
    case 0x00010: return "DVD-ROM";
    case 0x00040: return "BD-ROM";
    case 0x00043: return "BD-RE";
    case 0x00050: return "PS1 CD-ROM";
    case 0x00060: return "PS2 CD-ROM";
    case 0x00061: return "PS2 DVD-ROM";
    case 0x00070: return "PS3 DVD-ROM";
    case 0x00071: return "PS3 BD-ROM";
    case 0x10000: return "CD-DA";
    case 0x20000: return "SACD";
    default:      return "unknown";
    }
}