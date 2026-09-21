CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L

.PHONY: all clean test

all: cmd/shell cmd/csl

cmd/shell: cmd/shell.c cmd/shell.h cmd/posix_compat.h
	$(CC) $(CFLAGS) -o $@ cmd/shell.c

cmd/csl: cmd/csl.c cmd/csl_lang.c cmd/script.c cmd/csl_host.c cmd/csl_lang.h cmd/script.h cmd/csl_host.h
	$(CC) $(CFLAGS) -DCSL_HOST_PORT -o $@ cmd/csl.c cmd/csl_lang.c cmd/script.c cmd/csl_host.c

clean:
	rm -f cmd/shell cmd/csl

test: all
	@./cmd/csl examples/verify.csl 2>/dev/null | grep -q "ALL PASS" \
		&& echo "verify.csl: ALL PASS" \
		|| (echo "verify.csl: FAILED"; exit 1)