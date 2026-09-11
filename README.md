# CSL

CSL is a small POSIX shell and embeddable C shell API for CodeOS.

The implementation uses POSIX system interfaces only; it does not depend on
Linux-specific APIs, network services, or third-party libraries. POSIX
headers are isolated in `cmd/posix_compat.h`.

## Build

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic \
	-o cmd/shell cmd/shell.c
```

## Run

```sh
./cmd/shell
```

The shell supports external commands, pipelines, input/output redirection,
environment expansion, and the builtins `cd`, `pwd`, `export`, `unset`,
`exit`, `help`, `echo`, `env`, `printenv`, `which`, `type`, `history`,
`clear`, `true`, `false`, `version`, `mkdir`, `rmdir`, `touch`, `rm`, `cat`,
and `ls`. History keeps the most recent 1000 commands for the current shell
session.

## C API

Include `cmd/shell.h` to embed CSL in another POSIX program. Initialize a
`csl_shell_t` with streams, execute one line with `csl_shell_run_line`, or
run an input stream with `csl_shell_run`. The API returns the command status
and keeps the last status in `shell.last_status`.

To link the API into another program, compile the implementation with
`-DCSL_SHELL_NO_MAIN` and provide your own `main` function.
