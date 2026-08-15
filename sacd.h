#ifndef SACD_H
#define SACD_H

#include <stdint.h>

#define BD_DEVICE      0x0101000000000006ULL
#define SACD_ENABLE    0xff
#define SACD_DISABLE   0x53

int         sacd_open(int *fd);
int         sacd_close(int fd);

int         sacd_inquiry(int fd);
int         sacd_profile(int fd, uint32_t *profile);
int         sacd_d7_get(int fd, uint8_t *flag);
int         sacd_d7_set(int fd, uint8_t flag);

const uint8_t *sacd_iobuf(void);
const char    *sacd_profile_name(uint32_t profile);

#endif