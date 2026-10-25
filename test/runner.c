#include "result.h"
#include "use_case.h"
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

int main(int argc, char **argv) {
    if (argc < 3) {
        return EXIT_FAILURE;
    }
    char *p_uc = argv[1];
    char *p_bin = argv[2];

    int skipped_testsc = 0;
    char **skipped_testsv = nullptr;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--skip") == 0 && i + 1 < argc) {
            skipped_testsv = (char **)reallocarray(
                (void *)skipped_testsv, skipped_testsc + 2, sizeof(char *));
            if (!skipped_testsv) {
                perror("skipped tests");
                return EXIT_FAILURE;
            }
            skipped_testsv[skipped_testsc++] = argv[i + 1];
        }
    }
    skipped_testsv[skipped_testsc] = nullptr;

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
        for (int i = 0; i < skipped_testsc; i++) {
            if (strcmp(uc->name, skipped_testsv[i]) == 0) {
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
                perror("uc_inputfd");
                return EXIT_FAILURE;
            }

            char **exec_argv = uc_argv(uc);
            if (exec_argv == nullptr) {
                perror("argv");
                return EXIT_FAILURE;
            }

            if (chdir(uc->exec_path) == -1) {
                perror("chdir");
                return EXIT_FAILURE;
            }

            execv(p_bin_fp, exec_argv);
            perror("execv");
            return EXIT_FAILURE;
            break;
        default:
            int status;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                return EXIT_FAILURE;
            }

            Result r = result_init(status);
            AssertionResult *as = uc_assert_exit_status(uc, r);
            if (!as) {
                perror("assert");
                return EXIT_FAILURE;
            }

            if (as->pass) {
                printf("PASS %s\n", uc->name);
            } else {
                printf("FAIL %s; expected=%s actual=%s", uc->name, as->expected,
                       as->actual);
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
    free((void *)skipped_testsv);

    if (all_tests_passed) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
