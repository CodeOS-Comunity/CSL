#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static char *trim_whitespace(char *str) {
    char *end;

    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }

    return str;
}

static int split_command(char *line, char **argv, int max_args) {
    int argc = 0;
    char *token = strtok(line, " \t");

    while (token != NULL && argc < max_args - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    argv[argc] = NULL;
    return argc;
}

static void print_prompt(int root_mode) {
    printf("csl%s> ", root_mode ? "#" : "$" );
    fflush(stdout);
}

static int builtin_cd(char **argv, int argc) {
    const char *target = argc > 1 ? argv[1] : getenv("HOME");

    if (target == NULL) {
        fprintf(stderr, "cd: no target specified\n");
        return 1;
    }

    if (strcmp(target, "~") == 0) {
        target = getenv("HOME");
    }

    if (target == NULL || chdir(target) != 0) {
        perror("cd");
        return 1;
    }

    return 0;
}

static int builtin_pwd(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

static int builtin_echo(char **argv, int argc) {
    for (int i = 1; i < argc; i++) {
        printf("%s%s", argv[i], i + 1 < argc ? " " : "");
    }
    printf("\n");
    return 0;
}

static int builtin_ls(char **argv, int argc) {
    DIR *dir;
    struct dirent *entry;
    const char *path = argc > 1 ? argv[1] : ".";

    dir = opendir(path);
    if (dir == NULL) {
        perror("ls");
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
    return 0;
}

static int builtin_clear(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

static int builtin_help(void) {
    puts("Supported commands:");
    puts("  echo [text]     Print text");
    puts("  pwd             Print working directory");
    puts("  cd [dir]        Change directory");
    puts("  ls [dir]        List directory contents");
    puts("  clear           Clear the screen");
    puts("  mkdir [dir]     Create a directory");
    puts("  touch [file]    Create a file");
    puts("  rm [path]       Remove a file or directory");
    puts("  cat [file]      Print a file");
    puts("  root            Toggle sudo/root-like privileges");
    puts("  exec [file]     Execute a file or script");
    puts("  exit            Quit the shell");
    return 0;
}

static int builtin_mkdir(char **argv, int argc) {
    if (argc < 2) {
        fprintf(stderr, "mkdir: missing operand\n");
        return 1;
    }

    if (mkdir(argv[1], 0755) != 0) {
        perror("mkdir");
        return 1;
    }
    return 0;
}

static int builtin_touch(char **argv, int argc) {
    if (argc < 2) {
        fprintf(stderr, "touch: missing operand\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "a");
    if (fp == NULL) {
        perror("touch");
        return 1;
    }
    fclose(fp);
    return 0;
}

static int builtin_rm(char **argv, int argc) {
    if (argc < 2) {
        fprintf(stderr, "rm: missing operand\n");
        return 1;
    }

    if (remove(argv[1]) != 0) {
        perror("rm");
        return 1;
    }
    return 0;
}

static int builtin_cat(char **argv, int argc) {
    if (argc < 2) {
        fprintf(stderr, "cat: missing operand\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror("cat");
        return 1;
    }

    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        fwrite(buffer, 1, n, stdout);
    }

    fclose(fp);
    return 0;
}

static int builtin_root(int *root_mode) {
    if (*root_mode) {
        *root_mode = 0;
        puts("Root mode disabled.");
    } else {
        *root_mode = 1;
        puts("Root mode enabled. Commands will run with sudo-like privileges.");
    }
    return 0;
}

static int builtin_exec(char **argv, int argc, int root_mode) {
    if (argc < 2) {
        fprintf(stderr, "exec: missing operand\n");
        return 1;
    }

    if (root_mode) {
        printf("Executing %s with sudo-like privileges\n", argv[1]);
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (access(argv[1], X_OK) == 0) {
            execvp(argv[1], argv + 1);
        } else if (access(argv[1], F_OK) == 0) {
            execl("/bin/sh", "sh", argv[1], (char *)NULL);
        } else {
            fprintf(stderr, "exec: %s: not found\n", argv[1]);
            _exit(127);
        }

        perror("exec");
        _exit(127);
    } else if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    } else {
        perror("fork");
        return 1;
    }
}

int main(void) {
    char line[4096];
    int root_mode = 0;

    puts("CSL shell replica");
    puts("Type 'help' for supported commands.");

    while (1) {
        print_prompt(root_mode);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            putchar('\n');
            break;
        }

        line[strcspn(line, "\n")] = '\0';
        char *input = trim_whitespace(line);
        if (*input == '\0') {
            continue;
        }

        char *argv[64];
        int argc = split_command(input, argv, 64);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
            break;
        } else if (strcmp(argv[0], "help") == 0) {
            builtin_help();
        } else if (strcmp(argv[0], "echo") == 0) {
            builtin_echo(argv, argc);
        } else if (strcmp(argv[0], "pwd") == 0) {
            builtin_pwd();
        } else if (strcmp(argv[0], "cd") == 0) {
            builtin_cd(argv, argc);
        } else if (strcmp(argv[0], "ls") == 0) {
            builtin_ls(argv, argc);
        } else if (strcmp(argv[0], "clear") == 0) {
            builtin_clear();
        } else if (strcmp(argv[0], "mkdir") == 0) {
            builtin_mkdir(argv, argc);
        } else if (strcmp(argv[0], "touch") == 0) {
            builtin_touch(argv, argc);
        } else if (strcmp(argv[0], "rm") == 0) {
            builtin_rm(argv, argc);
        } else if (strcmp(argv[0], "cat") == 0) {
            builtin_cat(argv, argc);
        } else if (strcmp(argv[0], "root") == 0) {
            builtin_root(&root_mode);
        } else if (strcmp(argv[0], "exec") == 0) {
            builtin_exec(argv, argc, root_mode);
        } else {
            printf("%s: command not found\n", argv[0]);
        }
    }

    return 0;
}
