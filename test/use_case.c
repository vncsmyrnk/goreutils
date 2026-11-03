#include "use_case.h"
#include <asm-generic/errno-base.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    UC_ARGV_READ_INITIAL_CAP = 128,
    UC_COPY_WORKING_DIR_FILES_BUF_SIZE = 1024,
    UC_WORKING_DIR_PERM = 0755,
    UC_EXEC_DIR_PERM = 0644,
};

int uc_dup_input(UseCase *uc) {
    char *input_path =
        calloc(strlen(uc->path) + strlen("/input") + 1, sizeof(*uc->path));
    if (input_path == NULL) {
        return -1;
    }
    strncpy(input_path, uc->path, strlen(uc->path));
    strcat(input_path, "/input");

    int fd = open(input_path, O_RDONLY);
    if (fd == -1 && errno == ENOENT) {
        free(input_path);
        return 0;
    }
    free(input_path);
    if (fd == -1) {
        return -1;
    }

    int new_input_fd = dup2(fd, STDIN_FILENO);
    if (new_input_fd == -1) {
        close(fd);
        return -1;
    }
    close(fd);

    return 0;
}

int uc_dup_stdout(UseCase *uc) {
    if (!uc || !uc->exec_path) {
        return -1;
    }

    char *stdout_path =
        calloc(strlen(uc->exec_path) + strlen("/stdout") + 1, sizeof(char));
    if (!stdout_path) {
        return -1;
    }
    strncpy(stdout_path, uc->exec_path, strlen(uc->exec_path) + 1);
    strncat(stdout_path, "/stdout", strlen("/stdout") + 1);

    int fd = open(stdout_path, O_RDWR | O_CREAT | O_TRUNC, UC_EXEC_DIR_PERM);
    free(stdout_path);
    if (fd == -1) {
        return -1;
    }

    int rc = dup2(fd, STDOUT_FILENO);
    close(fd);
    if (rc == -1) {
        return -1;
    }

    return 0;
}

char **uc_argv(UseCase *uc) {
    char *argv_path =
        calloc(strlen(uc->path) + strlen("/argv") + 1, sizeof(*uc->path));
    if (argv_path == NULL) {
        return nullptr;
    }
    strncpy(argv_path, uc->path, strlen(uc->path));
    strcat(argv_path, "/argv");

    int fd = open(argv_path, O_RDONLY);
    free(argv_path);
    if (fd == -1 && errno != ENOENT) {
        return nullptr;
    }

    char **argv = (char **)calloc(2, sizeof(*argv));
    if (!argv) {
        close(fd);
        return nullptr;
    }
    argv[0] = ""; // first argument is the program's name
    argv[1] = nullptr;

    if (fd == -1) {
        errno = ENOENT;
        return argv;
    }

    size_t capacity = UC_ARGV_READ_INITIAL_CAP;
    size_t total_read = 0;
    char *argv_raw = malloc(capacity);
    if (!argv_raw) {
        return nullptr;
    }

    ssize_t b;
    while ((b = read(fd, argv_raw + total_read, capacity - total_read - 1))) {
        total_read += b;
        if (total_read < capacity) {
            continue;
        }
        capacity *= 2;
        char *new_argv_raw = realloc(argv_raw, capacity);
        if (!new_argv_raw) {
            free(argv_raw);
            close(fd);
            return nullptr;
        }
        argv_raw = new_argv_raw;
    }
    close(fd);
    if (b == -1) {
        free(argv_raw);
        free((void *)argv);
        return nullptr;
    }
    argv_raw[total_read] = '\0';

    int argc = 1;
    char *tok = strtok(argv_raw, "\n");
    while (tok) {
        char **new_argv =
            (char **)reallocarray((void *)argv, argc + 2, sizeof(*argv));
        if (!new_argv) {
            free(argv_raw);
            return nullptr;
        }
        argv = new_argv;
        argv[argc++] = tok;
        tok = strtok(nullptr, "\n");
    }
    argv[argc] = nullptr;

    return argv;
}

char *uc_exec_path(char *name) {
    if (!name) {
        return nullptr;
    }
    const char *tmp_path_prefix = "/tmp/goreutils-test-";
    const char *tmp_path_suffix = ".XXXXXX";
    char *tmp_path = calloc(strlen(tmp_path_prefix) + strlen(name) +
                                strlen(tmp_path_suffix) + 1,
                            sizeof(char));
    strncpy((void *)tmp_path, tmp_path_prefix, strlen(tmp_path_prefix));
    strncat(tmp_path, name, strlen(tmp_path));
    strncat(tmp_path, tmp_path_suffix, strlen(tmp_path_suffix));

    if (!mkdtemp(tmp_path)) {
        return nullptr;
    }

    return tmp_path;
}

char *uc_exec_path_working_dir(char *base_path) {
    char *working_dir_path =
        calloc(strlen(base_path) + strlen("/working-dir") + 1, sizeof(char));
    if (!working_dir_path) {
        return nullptr;
    }
    strncpy(working_dir_path, base_path, strlen(base_path));
    strncat(working_dir_path, "/working-dir", strlen("/working-dir") + 1);

    if (mkdir(working_dir_path, UC_WORKING_DIR_PERM) == -1) {
        free(working_dir_path);
        return nullptr;
    }
    return working_dir_path;
}

char *uc_src_path(char *name, char *base_path) {
    if (name == nullptr) {
        return nullptr;
    }
    char *path =
        calloc(strlen(base_path) + strlen(name) + 1, sizeof(base_path));
    if (path == nullptr) {
        return nullptr;
    }
    strncpy(path, base_path, strlen(base_path));
    strncat(path, "/", 2);
    strncat(path, name, strlen(name));
    return path;
}

// TODO: support directories on working dir
static int uc_build_working_dir(UseCase *uc) {
    if (!uc->path || !uc->exec_path_working_dir) {
        return -1;
    }

    char *working_dir_path = calloc(
        strlen(uc->path) + strlen("/working-dir") + 1, sizeof(*uc->path));
    if (!working_dir_path) {
        return -1;
    }
    strncpy(working_dir_path, uc->path, strlen(uc->path));
    strcat(working_dir_path, "/working-dir");

    DIR *dp = opendir(working_dir_path);
    if (!dp && errno != ENOENT) {
        free(working_dir_path);
        return -1;
    }
    if (!dp) {
        free(working_dir_path);
        return 0;
    }

    struct dirent *ep;
    while ((ep = readdir(dp))) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) {
            continue;
        }

        char *p1 = calloc(strlen(working_dir_path) + strlen(ep->d_name) + 2,
                          sizeof(char));
        if (!p1) {
            closedir(dp);
            free(working_dir_path);
            return -1;
        }
        strncpy(p1, working_dir_path, strlen(working_dir_path) + 1);
        strncat(p1, "/", 2);
        strncat(p1, ep->d_name, strlen(ep->d_name) + 1);

        int working_dir_fd = open(p1, O_RDONLY);
        if (working_dir_fd == -1 && errno != ENOENT) {
            closedir(dp);
            free(p1);
            free(working_dir_path);
            return -1;
        }

        char *p2 =
            calloc(strlen(uc->exec_path_working_dir) + strlen(ep->d_name) + 2,
                   sizeof(char));
        if (!p2) {
            closedir(dp);
            close(working_dir_fd);
            free(p1);
            free(working_dir_path);
            return -1;
        }
        strncpy(p2, uc->exec_path_working_dir,
                strlen(uc->exec_path_working_dir) + 1);
        strncat(p2, "/", 2);
        strncat(p2, ep->d_name, strlen(ep->d_name) + 1);

        int exec_working_dir_fd =
            open(p2, O_RDWR | O_CREAT | O_TRUNC, UC_EXEC_DIR_PERM);
        if (exec_working_dir_fd == -1 && errno != ENOENT) {
            closedir(dp);
            close(working_dir_fd);
            close(exec_working_dir_fd);
            free(p1);
            free(working_dir_path);
            free(p2);
            return -1;
        }

        char buf[UC_COPY_WORKING_DIR_FILES_BUF_SIZE];
        ssize_t b;
        while ((b = read(working_dir_fd, buf, sizeof(buf)))) {
            if (b < 1) {
                perror("read");
                return -1;
            }
            ssize_t written = write(exec_working_dir_fd, buf, b);
            if (written != b || written < 1) {
                perror("write");
                return -1;
            }
        }

        close(working_dir_fd);
        close(exec_working_dir_fd);
        free(p2);
        free(p1);
    }
    closedir(dp);

    free(working_dir_path);
    return 0;
}

UseCase *uc_init(char *name, char *base_path) {
    UseCase *uc = malloc(sizeof(UseCase));
    if (!uc) {
        return nullptr;
    }
    *uc = (UseCase){};

    uc->exec_path = uc_exec_path(name);
    if (!uc->exec_path) {
        uc_free(uc);
        free(uc);
        return nullptr;
    }

    uc->exec_path_working_dir = uc_exec_path_working_dir(uc->exec_path);
    if (!uc->exec_path_working_dir) {
        uc_free(uc);
        free(uc);
        return nullptr;
    }

    uc->name = calloc(strlen(name), sizeof(name));
    if (strncpy(uc->name, name, strlen(name)) == nullptr) {
        uc_free(uc);
        free(uc);
        return nullptr;
    }

    uc->path = uc_src_path(name, base_path);
    if (!uc->path) {
        uc_free(uc);
        free(uc);
        return nullptr;
    }

    if (uc_build_working_dir(uc) == -1) {
        uc_free(uc);
        free(uc);
        return nullptr;
    }

    return uc;
}

UseCase **ucs_read(char *path_to_use_cases) {
    DIR *dp = opendir(path_to_use_cases);
    if (dp == nullptr) {
        return nullptr;
    }

    int i = 0;
    UseCase **ucs = nullptr;
    struct dirent *ep;
    while ((ep = readdir(dp))) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) {
            continue;
        }
        UseCase *uc = uc_init(ep->d_name, path_to_use_cases);
        if (!uc) {
            continue;
        }
        UseCase **temp =
            (UseCase **)reallocarray((void *)ucs, i + 2, sizeof(*ucs));
        if (temp == nullptr) {
            closedir(dp);
            free((void *)ucs);
            return nullptr;
        }
        ucs = temp;
        ucs[i++] = uc;
    }
    closedir(dp);
    ucs[i] = nullptr;

    return ucs;
}

AssertionResult *uc_assert_exit_status(UseCase *uc, Result r) {
    char *expected_exit_status_path =
        calloc(strlen(uc->path) + strlen("/expect/exit_status") + 1,
               sizeof(*uc->path));
    if (!expected_exit_status_path) {
        return nullptr;
    }
    strncpy(expected_exit_status_path, uc->path, strlen(uc->path));
    strcat(expected_exit_status_path, "/expect/exit_status");
    int fd = open(expected_exit_status_path, O_RDONLY);
    if (fd == -1) {
        free(expected_exit_status_path);
        return nullptr;
    }
    free(expected_exit_status_path);

    char buf[3];
    ssize_t content = read(fd, buf, sizeof(buf));
    if (content < 1) {
        close(fd);
        return nullptr;
    }
    buf[content] = '\0';
    close(fd);

    AssertionResult *as = calloc(1, sizeof(AssertionResult));
    if (!as) {
        return nullptr;
    }

    char *expected = nullptr;
    bool pass = r.exit_code == atoi(buf);
    if (!pass) {
        int n = snprintf(expected, 0, "%d", r.exit_code);
        if (n < 0) {
            return nullptr;
        }
        expected = calloc(++n, sizeof(char));
        if (!expected) {
            return nullptr;
        }
        n = snprintf(expected, n, "%d", r.exit_code);
        if (n < 0) {
            free(expected);
            return nullptr;
        }
    }
    char *actual = calloc(strlen(buf), sizeof(char));
    if (!actual) {
        free(expected);
        return nullptr;
    }
    strncpy(actual, buf, strlen(buf));
    *as =
        (AssertionResult){.pass = pass, .expected = expected, .actual = actual};
    return as;
}

void uc_free(UseCase *uc) {
    if (!uc) {
        return;
    }
    if (uc->path) {
        free(uc->path);
    }
    if (uc->name) {
        free(uc->name);
    }
    if (uc->exec_path) {
        free(uc->exec_path);
    }
    if (uc->exec_path_working_dir) {
        free(uc->exec_path_working_dir);
    }
}

void as_free(AssertionResult *as) {
    if (!as) {
        return;
    }
    if (as->expected) {
        free(as->expected);
    }
    if (as->actual) {
        free(as->actual);
    }
}
