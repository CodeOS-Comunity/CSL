/*
 * csl.c — CSL runner: execute .csl scripts, or start a REPL when no file is
 * given. Links against the script engine (script.c) and the CSL stdlib
 * (csl_lang.c) ported from the CodeOS kernel.
 *
 * Script files are evaluated as a whole (newlines are statement separators),
 * so multi-line blocks (`while cond { ... }`, `if cond { ... } else { ... }`)
 * work exactly as they do when a script is pasted into the kernel console.
 */
#include "csl_lang.h"
#include "script.h"
#include "csl_host.h"

#include <stdio.h>
#include <string.h>

static void eval_whole(const char *code) {
    /* csl_eval() is line-oriented (skips leading '#', falls back to the
     * shell); whole-file evaluation uses the parser directly. */
    script_val_t res;
    int ret = script_eval(code, &res);
    if (ret == 0) {
        if (res.type == 0) kprintf("%lld\n", res.num);
        else if (res.str) kprintf("%s\n", res.str);
        if (res.str) free(res.str);
    } else {
        kprintf("csl: parse error\n");
    }
}

static int run_script_file(const char *path) {
    int size = 0, isdir = 0;
    if (fs_get_info(path, &size, &isdir) < 0 || isdir || size <= 0) {
        kprintf("csl: cannot open '%s'\n", path);
        return 1;
    }
    if (size > 256 * 1024) size = 256 * 1024;
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        kprintf("csl: OOM\n");
        return 1;
    }
    int n = fs_read(path, buf, size);
    if (n <= 0) {
        kprintf("csl: cannot read '%s'\n", path);
        free(buf);
        return 1;
    }
    buf[n] = 0;
    eval_whole(buf); /* whole-file evaluation; multi-line constructs allowed */
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    script_init();
    csl_init();

    if (argc > 1) {
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            if (run_script_file(argv[i]))
                rc = 1;
        }
        return rc;
    }

    /* Interactive REPL — each line is evaluated on its own. */
    char line[512];
    for (;;) {
        kprintf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (len == 0 || line[0] == '#') continue;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        csl_eval(line);
    }
    return 0;
}