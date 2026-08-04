#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 64
#define MAX_HISTORY 64
#define MAX_LINE 4096

static char *history[MAX_HISTORY];
static int history_count = 0;
static char current_dir[MAX_LINE];

static char *duplicate_string(const char *src);

static void append_history(const char *line) {
    char *copy = duplicate_string(line);
    if (copy == NULL) {
        fprintf(stderr, "csl: out of memory\n");
        return;
    }

    if (history_count < MAX_HISTORY) {
        history[history_count++] = copy;
    } else {
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; ++i) {
            history[i - 1] = history[i];
        }
        history[MAX_HISTORY - 1] = copy;
    }
}

static void free_history(void) {
    for (int i = 0; i < history_count; ++i) {
        free(history[i]);
    }
    history_count = 0;
}

static void print_history(void) {
    for (int i = 0; i < history_count; ++i) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

static char *duplicate_string(const char *src) {
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (dst != NULL) {
        memcpy(dst, src, len);
    }
    return dst;
}

static int tokenize_line(char *line, char **tokens, int max_tokens) {
    int count = 0;
    bool in_single = false;
    bool in_double = false;
    char buffer[MAX_LINE];
    size_t len = 0;

    while (*line != '\0') {
        if (isspace((unsigned char)*line)) {
            if (!in_single && !in_double && len > 0) {
                buffer[len] = '\0';
                tokens[count++] = duplicate_string(buffer);
                if (count >= max_tokens) {
                    return count;
                }
                len = 0;
            }
            ++line;
            continue;
        }

        if (*line == '\\' && line[1] != '\0') {
            buffer[len++] = line[1];
            line += 2;
            continue;
        }

        if (*line == '\'' && !in_double) {
            in_single = !in_single;
            ++line;
            continue;
        }

        if (*line == '"' && !in_single) {
            in_double = !in_double;
            ++line;
            continue;
        }

        buffer[len++] = *line++;
    }

    if (!in_single && !in_double && len > 0) {
        buffer[len] = '\0';
        tokens[count++] = duplicate_string(buffer);
    }

    return count;
}

static void free_tokens(char **tokens, int count) {
    for (int i = 0; i < count; ++i) {
        free(tokens[i]);
    }
}

static int builtin_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    puts("CSL shell built-ins:");
    puts("  help                 Show this message");
    puts("  exit, quit           Leave the shell");
    puts("  cd [dir]             Change directory");
    puts("  pwd                  Print working directory");
    puts("  echo [text]          Print text");
    puts("  history              Show command history");
    puts("  clear                Clear the screen");
    return 0;
}

static int kernel_chdir(const char *path) {
    return chdir(path);
}

static char *kernel_getcwd(char *buffer, size_t size) {
    return getcwd(buffer, size);
}

static void kernel_clear_screen(void) {
    fputs("\033[2J\033[H", stdout);
}

static const char *kernel_getenv(const char *name) {
    return getenv(name);
}

static int builtin_cd(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : kernel_getenv("HOME");
    if (target == NULL || target[0] == '\0') {
        target = ".";
    }
    if (kernel_chdir(target) != 0) {
        fprintf(stderr, "csl: cd: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (kernel_getcwd(current_dir, sizeof(current_dir)) != NULL) {
        puts(current_dir);
        return 0;
    }
    fprintf(stderr, "csl: pwd: %s\n", strerror(errno));
    return 1;
}

static int builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        fputs(argv[i], stdout);
        if (i + 1 < argc) {
            putchar(' ');
        }
    }
    putchar('\n');
    return 0;
}

static int builtin_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    kernel_clear_screen();
    return 0;
}

static int builtin_history(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_history();
    return 0;
}

static int run_external(const char *path, char *const argv[]) {
    (void)path;
    (void)argv;
    fprintf(stderr, "csl: external program execution is not implemented in this build\n");
    return 127;
}

static int execute_command(char **argv) {
    if (argv[0] == NULL) {
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        return -1;
    }

    if (strcmp(argv[0], "help") == 0) {
        return builtin_help(0, argv);
    }
    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(0, argv);
    }
    if (strcmp(argv[0], "pwd") == 0) {
        return builtin_pwd(0, argv);
    }
    if (strcmp(argv[0], "echo") == 0) {
        return builtin_echo(0, argv);
    }
    if (strcmp(argv[0], "clear") == 0) {
        return builtin_clear(0, argv);
    }
    if (strcmp(argv[0], "history") == 0) {
        return builtin_history(0, argv);
    }

    int status = run_external(argv[0], argv);
    if (status != 0) {
        fprintf(stderr, "csl: %s: command returned %d\n", argv[0], status);
    }
    return status;
}

int main(void) {
    char line[MAX_LINE];

    puts("CSL shell - custom kernel friendly shell");
    puts("Type 'help' for built-ins. Press Ctrl-D to exit.\n");

    for (;;) {
        if (isatty(STDIN_FILENO)) {
            fputs("CSL> ", stdout);
            fflush(stdout);
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        append_history(line);

        char *tokens[MAX_ARGS] = {0};
        int count = tokenize_line(line, tokens, MAX_ARGS);
        if (count == 0) {
            continue;
        }

        int status = execute_command(tokens);
        if (status < 0) {
            free_tokens(tokens, count);
            break;
        }

        free_tokens(tokens, count);
    }

    free_history();
    return 0;
}
