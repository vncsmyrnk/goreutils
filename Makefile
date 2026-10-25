all: cat tee

CFLAGS ?= -std=c23 -D_GNU_SOURCE

tee: tee.c
cat: cat.c

test_runner_tee: test/tee/runner.c
	$(CC) $(CFLAGS) $< -o $@

check: test_runner_tee
	./test_runner_tee ./test/tee/use-cases ./tee

clean:
	rm -f cat tee test_runner_tee

.PHONY: valgrind
valgrind: SHELL:=/bin/bash
valgrind: tee
	valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./tee $$(mktemp) < <(echo "test input")
	valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./test_runner_tee ./test/tee/use-cases ./tee
