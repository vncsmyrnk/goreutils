#ifndef RESULT_H
#define RESULT_H

typedef struct {
    int exit_code;
} Result;

Result result_init(int exit_code);

#endif // RESULT_H
