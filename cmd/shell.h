#ifndef CSL_SHELL_H
#define CSL_SHELL_H

#include <stdio.h>

typedef struct {
    FILE *input;
    FILE *output;
    FILE *error;
    int interactive;
    int should_exit;
    int last_status;
    char **history;
    size_t history_count;
    size_t history_capacity;
} csl_shell_t;

int csl_shell_init(csl_shell_t *shell, FILE *input, FILE *output, FILE *error);
void csl_shell_destroy(csl_shell_t *shell);
int csl_shell_run_line(csl_shell_t *shell, const char *line);
int csl_shell_run(csl_shell_t *shell);

#endif