#include "result.h"
#include "use_case.h"
#include "valgrind.h"
#include <asm-generic/errno-base.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **skipped_tests;
    bool no_valgrind;
} Flags;

const Flags default_flags = {.no_valgrind = false, .skipped_tests = nullptr};

int main(int argc, char **argv) {
    if (argc < 3) {
        return EXIT_FAILURE;
    }
    char *p_uc = argv[1];
    char *p_bin = argv[2];

    Flags *_flags = malloc(sizeof(Flags));
    memcpy(_flags, &default_flags, sizeof(Flags));

    _flags->skipped_tests = (char **)malloc(sizeof(char *));
    if (!_flags->skipped_tests) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    int skipped_tests_count = 0;
    _flags->skipped_tests[skipped_tests_count++] = nullptr;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--skip") == 0 && i + 1 < argc) {
            _flags->skipped_tests =
                (char **)reallocarray((void *)_flags->skipped_tests,
                                      skipped_tests_count + 1, sizeof(char *));
            if (!_flags->skipped_tests) {
                perror("skipped tests");
                return EXIT_FAILURE;
            }
            _flags->skipped_tests[skipped_tests_count++] = argv[i + 1];
            _flags->skipped_tests[skipped_tests_count] = nullptr;
        }
        if (strcmp(argv[i], "--no-valgrind") == 0) {
            _flags->no_valgrind = true;
        }
    }

    const Flags *const flags = _flags;
    _flags = nullptr;

    char *p_bin_fp = realpath(p_bin, nullptr);
    if (p_bin_fp == NULL) {
        perror("realpath");
        return EXIT_FAILURE;
    }

    UseCase **ucs = ucs_read(p_uc);
    if (ucs == nullptr) {
        free(p_bin_fp);
        perror("read from path");
        return EXIT_FAILURE;
    }
    bool all_tests_passed = true;

    UseCase **_ucs = ucs;
    for (_ucs = ucs; *_ucs != nullptr; _ucs++) {
        UseCase *uc = *_ucs;
        for (char **c = flags->skipped_tests; c != nullptr && *c != nullptr;
             c++) {
            if (strcmp(uc->name, *c) == 0) {
                printf("SKIP %s\n", uc->name);
                goto _continue;
            }
        }

        pid_t pid = fork();
        switch (pid) {
        case -1:
            perror("fork");
            return EXIT_FAILURE;
            break;
        case 0:
            if (uc_dup_input(uc) == -1) {
                perror("stdin dup");
                return EXIT_FAILURE;
            }

            if (uc_dup_stdout(uc) == -1) {
                perror("stdout dup");
                return EXIT_FAILURE;
            }

            if (uc_dup_stderr(uc) == -1) {
                perror("stderr dup");
                return EXIT_FAILURE;
            }

            UseCaseArgv *use_case_argv = uc_argv(uc);
            if (!use_case_argv) {
                perror("argv");
                return EXIT_FAILURE;
            }

            const char *exec_path = nullptr;
            if (!flags->no_valgrind) {
                exec_path = valgrind_exe;
            } else {
                exec_path = p_bin_fp;
            }

            char **exec_argv = nullptr;
            if (!flags->no_valgrind) {
                exec_argv = valgrind_uc_argv(p_bin_fp, use_case_argv);
                if (!exec_argv) {
                    perror("valgrind");
                    return EXIT_FAILURE;
                }
            } else {
                int i = 0;
                exec_argv =
                    (char **)malloc((use_case_argv->argc + 2) * sizeof(char *));
                if (!exec_argv) {
                    perror("malloc");
                    return EXIT_FAILURE;
                }
                exec_argv[i++] = "";
                for (char **c = use_case_argv->argv; *c != nullptr; c++) {
                    exec_argv[i++] = *c;
                }
                exec_argv[i] = nullptr;
            }

            if (chdir(uc->exec_path_working_dir) == -1) {
                perror("chdir");
                return EXIT_FAILURE;
            }

            execvp(exec_path, exec_argv);
            perror("execvp");
            return EXIT_FAILURE;
            break;
        default:
            int status;
            if (waitpid(pid, &status, WUNTRACED) < 0) {
                perror("waitpid");
                return EXIT_FAILURE;
            }

            Result r = result_init(WEXITSTATUS(status));
            AssertionResult *as = uc_assert_exit_status(uc, r);
            if (!as) {
                perror("assert");
                return EXIT_FAILURE;
            }

            if (as->pass) {
                printf("PASS %s\n", uc->name);
            } else {
                printf("FAIL %s; expected=%s actual=%s\n", uc->name,
                       as->expected, as->actual);
                all_tests_passed = false;
            }
            as_free(as);
            free(as);
        }
    _continue:
    }

    _ucs = ucs;
    while (*_ucs != nullptr) {
        uc_free(*_ucs);
        free(*_ucs);
        _ucs++;
    }
    free(p_bin_fp);
    free((void *)ucs);
    free((void *)flags->skipped_tests);
    free((void *)flags);

    if (all_tests_passed) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
