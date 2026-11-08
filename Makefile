CFLAGS ?= $(shell cat compile_flags.txt)
ifeq ($(CC), gcc)
	CFLAGS += -fanalyzer
endif

SRCS = $(wildcard *.c)
PROGRAMS = $(subst .c,,$(wildcard *.c))
test_OBJS = $(patsubst %.c,%.o,$(wildcard test/*.c))
test_SRCS = $(wildcard test/*.c)
test_PROGRAM = test/runner

VALGRIND = valgrind
VALGRIND_FLAGS = --tool=memcheck --leak-check=yes --track-origins=yes --error-exitcode=1 -s

.PHONY: all
all: $(PROGRAMS)

$(test_PROGRAM): $(test_OBJS)
	$(LINK.c) $^ -o $@


.PHONY: check
check: CFLAGS += -g
check: $(test_PROGRAM) $(PROGRAMS)
	@for t in $(PROGRAMS); do \
		echo "== ./$(test_PROGRAM)" "test/use-cases/$$t" "$$t" "--no-valgrind"; \
		"./$(test_PROGRAM)" "test/use-cases/$$t" "$$t" --no-valgrind --skip tee:no-files; \
		result=$$?; \
		if [ $$result -ne 0 ]; then \
			fail=$$result; \
		fi; \
	done; \
	[ -z $$fail ];

.PHONY: clean
clean:
	rm -f $(PROGRAMS) $(test_OBJS) $(test_PROGRAM)

.PHONY: lint
lint:
	clang-tidy $(SRCS) $(test_SRCS)

.PHONY: check-valgrind
check-valgrind: CFLAGS += -g
check-valgrind: $(test_PROGRAM) $(PROGRAMS)
	@for t in $(PROGRAMS); do \
		echo "== ./$(test_PROGRAM)" "test/use-cases/$$t" "$$t"; \
		"./$(test_PROGRAM)" "test/use-cases/$$t" "$$t" --skip tee:no-files; \
		result=$$?; \
		if [ $$result -ne 0 ]; then \
			fail=$$result; \
		fi; \
	done; \
	[ -z $$fail ];

.PHONY: test-valgrind
test-valgrind: CFLAGS += -g
test-valgrind: $(test_PROGRAM) $(PROGRAMS)
	@for t in $(PROGRAMS); do \
		echo "== ./$(test_PROGRAM)" "test/use-cases/$$t" "$$t" "--no-valgrind"; \
		$(VALGRIND) $(VALGRIND_FLAGS) "./$(test_PROGRAM)" "test/use-cases/$$t" "$$t" --no-valgrind --skip tee:no-files; \
		result=$$?; \
		if [ $$result -ne 0 ]; then \
			fail=$$result; \
		fi; \
	done; \
	[ -z $$fail ];
