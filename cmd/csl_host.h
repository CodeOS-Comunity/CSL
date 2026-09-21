#ifndef CSL_HOST_H
#define CSL_HOST_H

/*
 * csl_host.h — host (POSIX) shims for the CodeOS-kernel interfaces that the
 * script engine and CSL stdlib use. Building with `-DCSL_HOST_PORT` produces a
 * self-contained interpreter that runs on Linux/macOS/BSD; without it, the
 * same sources compile inside the CodeOS kernel against the real interfaces.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Kernel printf -> stdout. Supports %s, %lld, %d, %c, %x and friends. */
void kprintf(const char *fmt, ...);

/* Own implementations so we do not depend on glibc/BSD extensions. */
char  *strdup(const char *s);
size_t strlcpy(char *dst, const char *src, size_t size);

/* Shell integration (kernel shell.h). On host builds these run external
 * commands / read stdin so .csl scripts behave like they do on CodeOS. */
int shell_execute(const char *cmd);   /* run a command line, return exit code */
int shell_readln(char *buf, int bufsz); /* read a line from stdin, -1 on EOF */
int shell_kbhit(void);                /* 1 if a key is pending, else 0 */

/* scheduler + timer (kernel sched.h / drivers/timer.h) */
void    sched_sleep_ms(int ms);
int64_t timer_get_milliseconds(void);

/* filesystem (kernel fs.h) */
int fs_get_info(const char *path, int *size, int *isdir);
int fs_read(const char *path, char *buf, int size);

#endif /* CSL_HOST_H */