
#define _POSIX_C_SOURCE 200809L

#include "shell.h"
#include "posix_compat.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

#define CSL_MAX_ARGS 64
#define CSL_MAX_PIPELINE 16
#define CSL_MAX_HISTORY 1000

typedef struct {
	char *argv[CSL_MAX_ARGS];
	size_t argc;
	char *input;
	char *output;
	int append;
} csl_command_t;

typedef struct {
	csl_command_t commands[CSL_MAX_PIPELINE];
	size_t count;
} csl_pipeline_t;

static void free_pipeline(csl_pipeline_t *pipeline)
{
	size_t command_index;
	size_t argument_index;

	for (command_index = 0; command_index < pipeline->count; command_index++) {
		csl_command_t *command = &pipeline->commands[command_index];
		for (argument_index = 0; argument_index < command->argc; argument_index++)
			free(command->argv[argument_index]);
		free(command->input);
		free(command->output);
	}
	memset(pipeline, 0, sizeof(*pipeline));
}

static int append_token(csl_command_t *command, const char *token, size_t length)
{
	char *copy;

	if (command->argc + 1 >= CSL_MAX_ARGS) {
		fprintf(stderr, "csl: too many arguments\n");
		return -1;
	}
	copy = malloc(length + 1);
	if (copy == NULL)
		return -1;
	memcpy(copy, token, length);
	copy[length] = '\0';
	command->argv[command->argc++] = copy;
	command->argv[command->argc] = NULL;
	return 0;
}

static int append_redirect(char **destination, const char *token, size_t length)
{
	char *copy = malloc(length + 1);

	if (copy == NULL)
		return -1;
	memcpy(copy, token, length);
	copy[length] = '\0';
	free(*destination);
	*destination = copy;
	return 0;
}

static int is_builtin_name(const char *name)
{
	static const char *names[] = {
		"cd", "pwd", "export", "unset", "exit", "help", "echo",
		"env", "printenv", "which", "type", "history", "clear", "true",
		"false", "version", "mkdir", "rmdir", "touch", "rm", "cat", "ls",
		NULL
	};
	size_t index;

	for (index = 0; names[index] != NULL; index++) {
		if (strcmp(name, names[index]) == 0)
			return 1;
	}
	return 0;
}

static int record_history(csl_shell_t *shell, const char *line)
{
	char *copy;

	if (line[0] == '\0')
		return 0;
	copy = strdup(line);
	if (copy == NULL)
		return -1;
	if (shell->history_count == CSL_MAX_HISTORY) {
		free(shell->history[0]);
		memmove(shell->history, shell->history + 1,
				(CSL_MAX_HISTORY - 1) * sizeof(*shell->history));
		shell->history_count--;
	}
	if (shell->history_count == shell->history_capacity) {
		size_t capacity = shell->history_capacity == 0 ? 32 : shell->history_capacity * 2;
		char **history = realloc(shell->history, capacity * sizeof(*history));
		if (history == NULL) {
			free(copy);
			return -1;
		}
		shell->history = history;
		shell->history_capacity = capacity;
	}
	shell->history[shell->history_count++] = copy;
	return 0;
}

static const char *find_command(const char *name)
{
	static char path[4096];
	const char *path_list;
	const char *start;
	const char *end;

	if (strchr(name, '/') != NULL)
		return access(name, X_OK) == 0 ? name : NULL;
	path_list = getenv("PATH");
	if (path_list == NULL)
		return NULL;
	start = path_list;
	while (*start != '\0') {
		end = strchr(start, ':');
		if (end == NULL)
			end = start + strlen(start);
		if (end == start)
			snprintf(path, sizeof(path), "./%s", name);
		else
			snprintf(path, sizeof(path), "%.*s/%s", (int)(end - start), start, name);
		if (access(path, X_OK) == 0)
			return path;
		if (*end == '\0')
			break;
		start = end + 1;
	}
	return NULL;
}

static int builtin_echo(csl_shell_t *shell, csl_command_t *command)
{
	size_t index = 1;
	int newline = 1;

	if (index < command->argc && strcmp(command->argv[index], "-n") == 0) {
		newline = 0;
		index++;
	}
	for (; index < command->argc; index++)
		fprintf(shell->output, "%s%s", index == 1 || (index == 2 && !newline) ? "" : " ",
				command->argv[index]);
	if (newline)
		fputc('\n', shell->output);
	return 0;
}

static int builtin_env(csl_shell_t *shell, csl_command_t *command)
{
	char **entry;

	if (command->argc == 2) {
		const char *value = getenv(command->argv[1]);
		if (value != NULL)
			fprintf(shell->output, "%s\n", value);
		return value == NULL;
	}
	if (command->argc > 2)
		return 2;
	for (entry = environ; *entry != NULL; entry++)
		fprintf(shell->output, "%s\n", *entry);
	return 0;
}

static int builtin_history(csl_shell_t *shell, csl_command_t *command)
{
	size_t index;
	if (command->argc > 2)
		return 2;
	for (index = 0; index < shell->history_count; index++)
		fprintf(shell->output, "%zu  %s\n", index + 1, shell->history[index]);
	return 0;
}

static int builtin_filesystem(csl_shell_t *shell, csl_command_t *command)
{
	const char *name = command->argv[0];
	size_t index;

	if (strcmp(name, "mkdir") == 0) {
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			if (mkdir(command->argv[index], 0777) < 0) {
				fprintf(shell->error, "mkdir: %s: %s\n", command->argv[index], strerror(errno));
				return 1;
			}
		}
		return 0;
	}
	if (strcmp(name, "rmdir") == 0) {
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			if (rmdir(command->argv[index]) < 0) {
				fprintf(shell->error, "rmdir: %s: %s\n", command->argv[index], strerror(errno));
				return 1;
			}
		}
		return 0;
	}
	if (strcmp(name, "touch") == 0) {
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			int descriptor = open(command->argv[index], O_WRONLY | O_CREAT, 0666);
			if (descriptor < 0) {
				fprintf(shell->error, "touch: %s: %s\n", command->argv[index], strerror(errno));
				return 1;
			}
			close(descriptor);
		}
		return 0;
	}
	if (strcmp(name, "rm") == 0) {
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			if (unlink(command->argv[index]) < 0) {
				fprintf(shell->error, "rm: %s: %s\n", command->argv[index], strerror(errno));
				return 1;
			}
		}
		return 0;
	}
	if (strcmp(name, "cat") == 0) {
		char buffer[4096];
		if (command->argc == 1)
			return 0;
		for (index = 1; index < command->argc; index++) {
			int descriptor = strcmp(command->argv[index], "-") == 0 ? STDIN_FILENO :
				open(command->argv[index], O_RDONLY);
			ssize_t count;
			if (descriptor < 0) {
				fprintf(shell->error, "cat: %s: %s\n", command->argv[index], strerror(errno));
				return 1;
			}
			while ((count = read(descriptor, buffer, sizeof(buffer))) > 0)
				if (fwrite(buffer, 1, (size_t)count, shell->output) != (size_t)count)
					return 1;
			if (descriptor != STDIN_FILENO)
				close(descriptor);
			if (count < 0)
				return 1;
		}
		return 0;
	}
	return -1;
}

static int builtin_ls(csl_shell_t *shell, csl_command_t *command)
{
	int show_hidden = 0;
	size_t first_path = 1;
	size_t path_count;
	size_t path_index;

	if (first_path < command->argc && strcmp(command->argv[first_path], "-a") == 0) {
		show_hidden = 1;
		first_path++;
	}
	path_count = command->argc - first_path;
	if (path_count == 0)
		path_count = 1;
	for (path_index = 0; path_index < path_count; path_index++) {
		const char *path = command->argc == first_path ? "." : command->argv[first_path + path_index];
		DIR *directory = opendir(path);
		struct dirent *entry;

		if (directory == NULL) {
			fprintf(shell->error, "ls: %s: %s\n", path, strerror(errno));
			return 1;
		}
		if (path_count > 1)
			fprintf(shell->output, "%s:\n", path);
		while ((entry = readdir(directory)) != NULL) {
			if (!show_hidden && entry->d_name[0] == '.')
				continue;
			fprintf(shell->output, "%s\n", entry->d_name);
		}
		closedir(directory);
		if (path_count > 1 && path_index + 1 < path_count)
			fputc('\n', shell->output);
	}
	return 0;
}

static int parse_line(const char *line, csl_pipeline_t *pipeline)
{
	csl_command_t *command = &pipeline->commands[0];
	const char *cursor = line;

	pipeline->count = 1;
	while (*cursor != '\0') {
		char token[4096];
		size_t token_length = 0;
		int quote = 0;
		int escaped = 0;

		while (*cursor != '\0' && (escaped || quote ||
			   (*cursor != ' ' && *cursor != '\t' && *cursor != '\n' &&
				*cursor != '|' && *cursor != '<' && *cursor != '>'))) {
			if (escaped) {
				if (token_length + 1 >= sizeof(token))
					return -1;
				token[token_length++] = *cursor++;
				escaped = 0;
			} else if (*cursor == '\\' && quote != '\'') {
				escaped = 1;
				cursor++;
			} else if (*cursor == '$' && quote != '\'') {
				char variable[256];
				const char *value;
				const char *start;
				size_t variable_length;

				cursor++;
				if (*cursor == '{')
					cursor++;
				start = cursor;
				while (isalnum((unsigned char)*cursor) || *cursor == '_')
					cursor++;
				variable_length = (size_t)(cursor - start);
				if (variable_length == 0 || variable_length >= sizeof(variable))
					return -1;
				memcpy(variable, start, variable_length);
				variable[variable_length] = '\0';
				if (start[-1] == '{') {
					if (*cursor != '}')
						return -1;
					cursor++;
				}
				value = getenv(variable);
				if (value != NULL) {
					size_t value_length = strlen(value);
					if (token_length + value_length >= sizeof(token))
						return -1;
					memcpy(token + token_length, value, value_length);
					token_length += value_length;
				}
			} else if ((*cursor == '\'' || *cursor == '"') && quote == 0) {
				quote = *cursor++;
			} else if (*cursor == quote) {
				quote = 0;
				cursor++;
			} else {
				if (token_length + 1 >= sizeof(token))
					return -1;
				token[token_length++] = *cursor++;
			}
		}
		if (quote || escaped)
			return -1;
		if (token_length > 0 && append_token(command, token, token_length) < 0)
			return -1;

		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n')
			cursor++;
		if (*cursor == '<' || *cursor == '>') {
			char redirect = *cursor++;
			const char *start;

			if (redirect == '>' && *cursor == '>') {
				command->append = 1;
				cursor++;
			}
			while (*cursor == ' ' || *cursor == '\t')
				cursor++;
			start = cursor;
			while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
				   *cursor != '|' && *cursor != '<' && *cursor != '>')
				cursor++;
			if (cursor == start)
				return -1;
			if (redirect == '<') {
				if (append_redirect(&command->input, start,
									(size_t)(cursor - start)) < 0)
					return -1;
			} else {
				if (append_redirect(&command->output, start,
									(size_t)(cursor - start)) < 0)
					return -1;
			}
		} else if (*cursor == '|') {
			if (command->argc == 0 || pipeline->count >= CSL_MAX_PIPELINE)
				return -1;
			cursor++;
			command = &pipeline->commands[pipeline->count++];
		} else if (*cursor != '\0') {
			continue;
		}
	}
	return 0;
}

static int run_builtin(csl_shell_t *shell, csl_command_t *command)
{
	const char *name = command->argv[0];

	if (strcmp(name, "cd") == 0) {
		const char *directory = command->argc > 1 ? command->argv[1] : getenv("HOME");
		if (directory == NULL || chdir(directory) < 0) {
			fprintf(shell->error, "cd: %s\n", strerror(errno));
			return 1;
		}
		return 0;
	}
	if (strcmp(name, "pwd") == 0) {
		char directory[4096];
		if (getcwd(directory, sizeof(directory)) == NULL) {
			fprintf(shell->error, "pwd: %s\n", strerror(errno));
			return 1;
		}
		fprintf(shell->output, "%s\n", directory);
		return 0;
	}
	if (strcmp(name, "export") == 0) {
		char *equals;
		if (command->argc != 2 || (equals = strchr(command->argv[1], '=')) == NULL) {
			fprintf(shell->error, "usage: export NAME=value\n");
			return 2;
		}
		*equals = '\0';
		return setenv(command->argv[1], equals + 1, 1) < 0;
	}
	if (strcmp(name, "unset") == 0) {
		size_t index;
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++)
			if (unsetenv(command->argv[index]) < 0)
				return 1;
		return 0;
	}
	if (strcmp(name, "exit") == 0) {
		shell->should_exit = 1;
		return command->argc > 1 ? atoi(command->argv[1]) : shell->last_status;
	}
	if (strcmp(name, "help") == 0) {
		fprintf(shell->output,
				"builtins: cd pwd export unset exit help echo env printenv which "
				"type history clear true false version mkdir rmdir touch rm cat ls\n");
		return 0;
	}
	if (strcmp(name, "echo") == 0)
		return builtin_echo(shell, command);
	if (strcmp(name, "env") == 0 || strcmp(name, "printenv") == 0)
		return builtin_env(shell, command);
	if (strcmp(name, "history") == 0)
		return builtin_history(shell, command);
	if (strcmp(name, "which") == 0) {
		size_t index;
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			const char *path = find_command(command->argv[index]);
			if (path == NULL)
				return 1;
			fprintf(shell->output, "%s\n", path);
		}
		return 0;
	}
	if (strcmp(name, "type") == 0) {
		size_t index;
		if (command->argc < 2)
			return 2;
		for (index = 1; index < command->argc; index++) {
			if (is_builtin_name(command->argv[index]))
				fprintf(shell->output, "%s is a shell builtin\n", command->argv[index]);
			else {
				const char *path = find_command(command->argv[index]);
				if (path == NULL)
					return 1;
				fprintf(shell->output, "%s is %s\n", command->argv[index], path);
			}
		}
		return 0;
	}
	if (strcmp(name, "clear") == 0) {
		fputs("\033[H\033[2J", shell->output);
		return 0;
	}
	if (strcmp(name, "true") == 0)
		return 0;
	if (strcmp(name, "false") == 0)
		return 1;
	if (strcmp(name, "version") == 0) {
		fprintf(shell->output, "CSL POSIX shell 1.0\n");
		return 0;
	}
	if (strcmp(name, "ls") == 0)
		return builtin_ls(shell, command);
	if (strcmp(name, "mkdir") == 0 || strcmp(name, "rmdir") == 0 ||
			strcmp(name, "touch") == 0 || strcmp(name, "rm") == 0 ||
			strcmp(name, "cat") == 0)
		return builtin_filesystem(shell, command);
	return -1;
}

static int redirect_command(csl_command_t *command)
{
	int descriptor;

	if (command->input != NULL) {
		descriptor = open(command->input, O_RDONLY);
		if (descriptor < 0 || dup2(descriptor, STDIN_FILENO) < 0)
			return -1;
		close(descriptor);
	}
	if (command->output != NULL) {
		int flags = O_WRONLY | O_CREAT | (command->append ? O_APPEND : O_TRUNC);
		descriptor = open(command->output, flags, 0666);
		if (descriptor < 0 || dup2(descriptor, STDOUT_FILENO) < 0)
			return -1;
		close(descriptor);
	}
	return 0;
}

static int run_pipeline(csl_shell_t *shell, csl_pipeline_t *pipeline)
{
	pid_t children[CSL_MAX_PIPELINE];
	int previous_input = -1;
	int status = 0;
	size_t index;

	for (index = 0; index < pipeline->count; index++) {
		int pipe_fds[2] = {-1, -1};
		pid_t child;

		if (index + 1 < pipeline->count && pipe(pipe_fds) < 0)
			return 1;
		child = fork();
		if (child == 0) {
			int builtin_status;
			if (previous_input >= 0)
				dup2(previous_input, STDIN_FILENO);
			if (pipe_fds[1] >= 0)
				dup2(pipe_fds[1], STDOUT_FILENO);
			if (redirect_command(&pipeline->commands[index]) < 0)
				_exit(1);
			builtin_status = run_builtin(shell, &pipeline->commands[index]);
			if (builtin_status >= 0)
				_exit(builtin_status);
			execvp(pipeline->commands[index].argv[0], pipeline->commands[index].argv);
			fprintf(shell->error, "%s: %s\n", pipeline->commands[index].argv[0], strerror(errno));
			_exit(127);
		}
		if (child < 0)
			return 1;
		children[index] = child;
		close(previous_input);
		close(pipe_fds[1]);
		previous_input = pipe_fds[0];
	}
	close(previous_input);
	for (index = 0; index < pipeline->count; index++) {
		if (waitpid(children[index], &status, 0) < 0)
			return 1;
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int csl_shell_init(csl_shell_t *shell, FILE *input, FILE *output, FILE *error)
{
	if (shell == NULL)
		return -1;
	shell->input = input != NULL ? input : stdin;
	shell->output = output != NULL ? output : stdout;
	shell->error = error != NULL ? error : stderr;
	shell->interactive = isatty(fileno(shell->input));
	shell->should_exit = 0;
	shell->last_status = 0;
	shell->history = NULL;
	shell->history_count = 0;
	shell->history_capacity = 0;
	return 0;
}

void csl_shell_destroy(csl_shell_t *shell)
{
	size_t index;

	if (shell == NULL)
		return;
	for (index = 0; index < shell->history_count; index++)
		free(shell->history[index]);
	free(shell->history);
	shell->history = NULL;
	shell->history_count = 0;
	shell->history_capacity = 0;
}

int csl_shell_run_line(csl_shell_t *shell, const char *line)
{
	csl_pipeline_t pipeline = {0};
	int builtin_status;

	if (shell == NULL || line == NULL)
		return -1;
	if (record_history(shell, line) < 0)
		return -1;
	if (parse_line(line, &pipeline) < 0) {
		fprintf(shell->error, "csl: syntax error\n");
		free_pipeline(&pipeline);
		shell->last_status = 2;
		return shell->last_status;
	}
	if (pipeline.commands[0].argc == 0) {
		free_pipeline(&pipeline);
		return shell->last_status;
	}
	if (pipeline.count == 1 && pipeline.commands[0].input == NULL &&
		pipeline.commands[0].output == NULL) {
		builtin_status = run_builtin(shell, &pipeline.commands[0]);
		if (builtin_status >= 0) {
			free_pipeline(&pipeline);
			shell->last_status = builtin_status;
			return builtin_status;
		}
	}
	shell->last_status = run_pipeline(shell, &pipeline);
	free_pipeline(&pipeline);
	return shell->last_status;
}

int csl_shell_run(csl_shell_t *shell)
{
	char *line = NULL;
	size_t capacity = 0;

	if (shell == NULL)
		return -1;
	while (!shell->should_exit) {
		ssize_t length;
		if (shell->interactive)
			fprintf(shell->output, "csl$ ");
		fflush(shell->output);
		length = getline(&line, &capacity, shell->input);
		if (length < 0)
			break;
		if (length > 0 && line[length - 1] == '\n')
			line[length - 1] = '\0';
		csl_shell_run_line(shell, line);
	}
	free(line);
	return shell->last_status;
}

#ifndef CSL_SHELL_NO_MAIN
int main(void)
{
	csl_shell_t shell;

	if (csl_shell_init(&shell, stdin, stdout, stderr) < 0)
		return 1;
	{
		int status = csl_shell_run(&shell);
		csl_shell_destroy(&shell);
		return status;
	}
}
#endif
