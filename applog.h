#ifndef APPLOG_H
#define APPLOG_H

#include <stdint.h>

#define LOG_MAX_LINES 640
#define LOG_MAX_COLS  110

void        log_init(void);
void        log_line(const char *s);
void        log_fmt(const char *fmt, ...);
void        log_hex(const char *label, const uint8_t *b, int len);
void        log_dump(const char *label, const uint8_t *b, int len);
int         log_count(void);
const char *log_at(int idx);
void        log_probe_sinks(void);
int         log_probe_step(void);
int         log_flush(void);
const char *log_sink_path(void);

#endif