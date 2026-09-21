/*
 * csl_host.c — POSIX implementations of the kernel interfaces CSL needs.
 * See csl_host.h for the API.
 */
#include "csl_host.h"

#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

char *strdup(const char *s) {
    size_t n;
    char *p;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) {
        size_t c = (n < size - 1) ? n : (size - 1);
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return n;
}

/* Split `cmd` into argv (whitespace separated, double-quoted strings keep
 * their contents) and run it. Returns the child exit code, or -1 if the
 * program could not be launched. */
static int run_command(char *cmd) {
    char *argv[64];
    int argc = 0;
    char *p = cmd;

    while (*p && argc < 63) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = 0;
            /* skip trailing junk up to next token */
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
        }
    }
    argv[argc] = 0;
    if (argc == 0) return 0;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int shell_execute(const char *cmd) {
    if (!cmd || !*cmd) return -1;
    char *copy = strdup(cmd);
    if (!copy) return -1;
    int rc = run_command(copy);
    free(copy);
    return rc;
}

int shell_readln(char *buf, int bufsz) {
    if (!buf || bufsz <= 0) return -1;
    if (!fgets(buf, bufsz, stdin)) return -1;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return (int)n;
}

int shell_kbhit(void) {
    return 0; /* non-interactive builds have no key-press bridge */
}

void sched_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, 0);
}

int64_t timer_get_milliseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int fs_get_info(const char *path, int *size, int *isdir) {
    struct stat st;
    if (!path || stat(path, &st) != 0) return -1;
    if (size) *size = (int)st.st_size;
    if (isdir) *isdir = S_ISDIR(st.st_mode) ? 1 : 0;
    return 0;
}

int fs_read(const char *path, char *buf, int size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return (int)n;
}