#include "use_case.h"
#include <string.h>

const char *const valgrind_argv[] = {"--tool=memcheck",
                                     "--leak-check=yes",
                                     "--track-origins=yes",
                                     "--error-exitcode=1",
                                     "-s",
                                     nullptr};
const int valgrind_argc = sizeof(valgrind_argv) / sizeof(valgrind_argv[0]);

char **valgrind_uc_argv(const char *bin_path, const UseCaseArgv *u) {
    char **exec_argv =
        (char **)malloc((valgrind_argc + u->argc + 3) * sizeof(char *));
    if (!exec_argv) {
        return nullptr;
    }

    int i = 0;
    exec_argv[i++] = "";
    for (const char *const *c = valgrind_argv; *c != nullptr; c++) {
        exec_argv[i++] = strdup(*c);
    }
    exec_argv[i++] = strdup(bin_path);
    for (char **c = u->argv; *c != nullptr; c++) {
        exec_argv[i++] = *c;
    }
    exec_argv[i] = nullptr;
    return exec_argv;
}
