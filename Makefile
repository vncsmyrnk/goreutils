all: cat tee

tee: tee.c
cat: cat.c

clean:
	rm -f cat tee

.PHONY: valgrind
valgrind: SHELL:=/bin/bash
valgrind: tee
	valgrind --tool=memcheck --leak-check=yes --error-exitcode=1 ./tee $$(mktemp) < <(echo "test input")
