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

typedef struct SkippedTest {
    const char *basename_bin;
    const char *name;
} SkippedTest;

typedef struct {
    char **skipped_tests;
    SkippedTest **skipped;
    bool no_valgrind;
    char **argv;
    int argc;
} Flags;

const Flags default_flags = {.no_valgrind = false, .skipped_tests = nullptr};

const Flags *flags_parse(int argc, const char *argv[]) {
    Flags *f = malloc(sizeof(Flags));
    if (!f) {
        return nullptr;
    }
    memcpy(f, &default_flags, sizeof(Flags));

    f->argv = (char **)malloc((argc + 1) * sizeof(char *));
    if (!f->argv) {
        return nullptr;
    }

    f->skipped = (SkippedTest **)malloc(sizeof(SkippedTest *));
    if (!f->skipped) {
        return nullptr;
    }

    int skipped_tests_count = 0;
    f->skipped[skipped_tests_count] = nullptr;
    f->argc = 0;
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] != '-') {
            f->argv[f->argc++] = strdup(argv[i]);
            f->argv[f->argc] = nullptr;
            continue;
        }

        if (strcmp(argv[i], "--skip") == 0 && i + 1 < argc) {
            SkippedTest **new_skipped = (SkippedTest **)realloc(
                (void *)f->skipped, 2 * sizeof(SkippedTest *));
            if (!new_skipped) {
                return nullptr;
            }
            f->skipped = new_skipped;

            char *argument = strdup(argv[i + 1]);
            char *basename_bin = strtok(argument, ":");
            if (!basename_bin) {
                free(argument);
                continue;
            }
            char *name = strtok(nullptr, ":");
            if (!name) {
                free(argument);
                continue;
            }

            SkippedTest *t = malloc(sizeof(SkippedTest));
            if (!t) {
                free(argument);
                return nullptr;
            }
            *t = (SkippedTest){.basename_bin = strdup(basename_bin),
                               .name = strdup(name)};
            free(argument);

            f->skipped[skipped_tests_count++] = t;
            f->skipped[skipped_tests_count] = nullptr;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--no-valgrind") == 0) {
            f->no_valgrind = true;
        }
    }

    return f;
}

void flags_free(const Flags *f) {
    for (SkippedTest **_s = f->skipped; *_s != nullptr; _s++) {
        SkippedTest *s = *_s;
        if (s->basename_bin) {
            free((void *)s->basename_bin);
        }
        if (s->name) {
            free((void *)s->name);
        }
        free((void *)s);
    }
    if (f->skipped) {
        free((void *)f->skipped);
    }
    for (char **a = f->argv; *a != nullptr; a++) {
        free(*a);
    }
    if (f->argv) {
        free((void *)f->argv);
    }
}

int main(int argc, const char **argv) {
    const Flags *const flags = flags_parse(argc, argv);
    if (!flags) {
        perror("flags");
        return EXIT_FAILURE;
    }

    if (flags->argc < 3) {
        return EXIT_FAILURE;
    }

    const char *p_uc = flags->argv[1];
    const char *p_bin = flags->argv[2];

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
        for (SkippedTest **s = flags->skipped; s != nullptr && *s != nullptr;
             s++) {
            if (strcmp(uc->name, (*s)->name) == 0) {
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
                printf("PASS %s; %s\n", p_bin, uc->name);
            } else {
                printf("FAIL %s; %s; expected=%s actual=%s\n", p_bin, uc->name,
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
    flags_free(flags);
    free((void *)flags);

    if (all_tests_passed) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
