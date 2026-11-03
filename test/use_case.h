#ifndef USECASE_H
#define USECASE_H

#include "result.h"
#include <stdlib.h>

typedef struct {
    char *path;
    char *exec_path;
    char *exec_path_working_dir;
    char *name;
} UseCase;

typedef struct {
    char *expected;
    char *actual;
    bool pass;
} AssertionResult;

UseCase **ucs_read(char *path_to_use_cases);
int uc_dup_input(UseCase *uc);
int uc_dup_stdout(UseCase *uc);
char **uc_argv(UseCase *uc);
void uc_free(UseCase *uc);
AssertionResult *uc_assert_exit_status(UseCase *uc, Result r);
void as_free(AssertionResult *as);

#endif // USECASE_H
