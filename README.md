# CSL

CSL is the command shell and scripting language of CodeOS. This repository
mirrors the current in-tree implementation, which has two parts:

```text
cmd/shell         POSIX shell — interactive, pipelines, redirection, env
                  expansion, file/dir builtins. Embeddable via cmd/shell.h
                  (compile with -DCSL_SHELL_NO_MAIN to link the API).
cmd/csl           Scripting language runner — the CodeOS scripting language
                  (script engine + CSL stdlib) with whole-file and REPL modes.
examples/*.csl    Example programs in the scripting language (6 games +
                  verify.csl self-test).
```

## Build

```sh
make              # builds both cmd/shell and cmd/csl
```

Requirements: a C99 compiler + POSIX. No third-party dependencies.

To link the shell API into another program, compile the implementation with
`-DCSL_SHELL_NO_MAIN` and provide your own `main` function.

## The shell (`./cmd/shell`)

The shell supports external commands, pipelines, input/output redirection,
environment expansion, and the builtins `cd`, `pwd`, `export`, `unset`,
`exit`, `help`, `echo`, `env`, `printenv`, `which`, `type`, `history`,
`clear`, `true`, `false`, `version`, `mkdir`, `rmdir`, `touch`, `rm`, `cat`,
and `ls`. History keeps the most recent 1000 commands for the current shell
session.

Use it as an embeddable C API: include `cmd/shell.h`, initialize a
`csl_shell_t` with streams, execute one line with `csl_shell_run_line`, or
run an input stream with `csl_shell_run`.

## The scripting language (`./cmd/csl`)

A user-friendly scripting language built on a small expression-tree engine
(ported from `kernel/kernel/script.c`) with the CSL standard library
(ported from `kernel/kernel/csl.c`): string, math, and collection builtins,
system calls, and control flow.

Run a script:

```sh
./cmd/csl examples/math.csl
./cmd/csl examples/verify.csl      # self-test, expects "ALL PASS"
```

Or start a REPL:

```sh
./cmd/csl
> print "hello " + "world"
> let n = 41
> print n + 1
```

The examples are the games shipped with CodeOS
(`kernel/userspace/games/*.csl`):

| example          | game                       |
|------------------|----------------------------|
| `guess.csl`      | guess the number (1-100)   |
| `hangman.csl`    | guess the secret word      |
| `math.csl`       | arithmetic drill           |
| `mines.csl`      | minesweeper (5x5, 5 mines) |
| `reflex.csl`     | reaction timer             |
| `rps.csl`        | rock/paper/scissors, best of 5 |

### Language notes

- Statements: `let`/`const`, `if`/`else`, `while`, C-style
  `for (init; cond; update)`, `function` + `return`, `break`/`continue`,
  `try`/`catch`/`throw`.
- Values are numbers (int64) or strings; `true`/`false`/`null`/`this` exist.
- `print` and `printn` write to stdout; `readln` reads a line; `shell`
  runs an external command and returns its exit code.
- Arrays are string reprs like `[1, 2, 3]`. `push`/`pop` are immutable —
  assign the result back: `arr = push(arr, 4)`. Access with `at`,
  `first`, `last`, `lcontains`, `len`.
- `for` is C-style (the kernel grammar has no Python-style `for x in …`).
- Multiple statements/expressions per line are separated with `;`; newlines
  are statement separators too, so multi-line blocks work.

### Builtins

Strings: `str`, `num`, `len`, `upper`, `lower`, `trim`, `reverse`, `substr`,
`instr`, `replace`, `contains`, `startswith`, `endswith`, `chrat`, `ord`,
`chr`, `repeat`, `split`, `join`, `padl`, `padr`, `countsub`, `replaceall`,
`title`.

Math: `abs`, `min`, `max`, `clamp`, `pow`, `sqrt`, `gcd`, `lcm`, `idiv`,
`mod`, `floor`, `ceil`, `round`, `rand`, `sum`, `cat`, `isprime`, `fact`,
`iseven`, `isodd`, `sign`, `digits`, `hex`, `unhex`, `bitand`, `bitor`,
`bitxor`, `bnot`, `shl`, `shr`.

Collections: `at`, `keys`, `push`, `pop`, `sort`, `first`, `last`,
`lreverse`, `lcontains`.

System: `sleep`, `ticks`, `shell`, `readln`, `kbhit`, plus `type`, `printn`,
`dump`.

## Host-port notes

The scripting language sources are a self-contained POSIX port of the
CodeOS-kernel implementation:

- Kernel-only interfaces (`kprintf`, the x11_* canvas bridge, scheduler and
  timer calls, the kernel filesystem) are provided by `cmd/csl_host.c` on
  this host build. The x11_* canvas bridge is compiled out with
  `CSL_HOST_PORT` (it is kernel-only and guarded in `csl_lang.c`).
- Two fixes are applied on top of the kernel sources (marked in the code):
  1. `parse_mul` parses its first operand with `parse_unary()`, so leading
     unary `not` / `-` work. The kernel original used `parse_postfix()`,
     which skipped unary operators at the start of an expression.
  2. `vars_save`/`vars_restore` use a stack of frames instead of a single
     save slot, so recursive functions that make more than one call per body
     (e.g. `fib(n-1) + fib(n-2)`) restore variables correctly.
- `csl_run_file()` in the kernel evaluates scripts line-by-line. The runner
  here evaluates whole files instead, so multi-line blocks work in scripts;
  the REPL still evaluates line-by-line like the kernel console.

## Mirror status

| path in this repo   | in-tree source                                   |
|---------------------|--------------------------------------------------|
| `cmd/shell.c/.h`    | standalone POSIX shell                           |
| `cmd/script.c/.h`   | `kernel/kernel/script.c/.h` (host port)          |
| `cmd/csl_lang.c/.h` | `kernel/kernel/csl.c/csl.h` (host port)          |
| `examples/*.csl`    | `kernel/userspace/games/*.csl`                   |
| `cmd/csl_host.c/.h` | host shims for the kernel interfaces (new)       |

## License

GPL-3.0 (see LICENSE), matching the CodeOS kernel.