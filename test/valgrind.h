#ifndef VALGRIND_H
#define VALGRIND_H

#include "use_case.h"

const char *const valgrind_exe = "valgrind";

char **valgrind_uc_argv(const char *bin_path, const UseCaseArgv *u);

#endif // VALGRIND_H
