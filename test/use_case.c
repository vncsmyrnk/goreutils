#include "use_case.h"
#include <asm-generic/errno-base.h>
#include <bits/types/error_t.h>
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
    if (uc->opts->no_stdin) {
        return close(STDIN_FILENO);
    }
    char *input_path =
        calloc(strlen(uc->path) + strlen("/input") + 1, sizeof(*uc->path));
    if (input_path == NULL) {
        return -1;
    }
    strncpy(input_path, uc->path, strlen(uc->path));
    strcat(input_path, "/input");

    int fd = open(input_path, O_RDONLY);
    free(input_path);
    if (fd == -1 && errno == ENOENT) {
        return 0;
    }
    if (fd == -1) {
        return -1;
    }

    int new_input_fd = dup2(fd, STDIN_FILENO);
    close(fd);
    if (new_input_fd == -1) {
        return -1;
    }

    return 0;
}

static int uc_dup_exec_path_fd(UseCase *uc, int oldfd,
                               const char *exec_path_file_name) {
    if (!uc || !uc->exec_path) {
        return -1;
    }

    char *stdout_path = calloc(strlen(uc->exec_path) + strlen("/") +
                                   strlen(exec_path_file_name) + 1,
                               sizeof(char));
    if (!stdout_path) {
        return -1;
    }

    strncpy(stdout_path, uc->exec_path, strlen(uc->exec_path) + 1);
    strncat(stdout_path, "/", 2);
    strncat(stdout_path, exec_path_file_name, strlen(exec_path_file_name) + 1);

    int newfd = open(stdout_path, O_RDWR | O_CREAT | O_TRUNC, UC_EXEC_DIR_PERM);
    free(stdout_path);
    if (newfd == -1) {
        return -1;
    }

    int rc = dup2(newfd, oldfd);
    close(newfd);
    if (rc == -1) {
        return -1;
    }

    close(newfd);
    return 0;
}

int uc_dup_stdout(UseCase *uc) {
    if (uc->opts->no_stdout) {
        return close(STDOUT_FILENO);
    }
    return uc_dup_exec_path_fd(uc, STDOUT_FILENO, "stdout");
}

int uc_dup_stderr(UseCase *uc) {
    if (uc->opts->no_stderr) {
        return close(STDERR_FILENO);
    }
    return uc_dup_exec_path_fd(uc, STDERR_FILENO, "stderr");
}

UseCaseArgv *uc_argv(UseCase *uc) {
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

    UseCaseArgv *a = malloc(sizeof(UseCaseArgv));
    if (!a) {
        return nullptr;
    }

    a->argc = 0;
    a->argv = (char **)calloc(1, sizeof(char *));
    if (!a->argv) {
        close(fd);
        return nullptr;
    }

    a->argv[a->argc] = nullptr;
    if (fd == -1) {
        errno = ENOENT;
        return a;
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
        return nullptr;
    }
    argv_raw[total_read] = '\0';

    char *tok = strtok(argv_raw, "\n");
    while (tok) {
        char **new_argv =
            (char **)reallocarray((void *)a->argv, a->argc + 2, sizeof(char *));
        if (!new_argv) {
            free(argv_raw);
            return nullptr;
        }
        a->argv = new_argv;
        a->argv[a->argc++] = tok;
        tok = strtok(nullptr, "\n");
    }
    a->argv[a->argc] = nullptr;

    return a;
}

char *uc_exec_path(const char *name) {
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

char *uc_exec_path_working_dir(const char *base_path) {
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

char *uc_src_path(const char *name, const char *base_path) {
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

UseCaseOptions *uc_opts(UseCase *uc) {
    char *options_fp =
        malloc((strlen(uc->path) + strlen("/options") + 1) * sizeof(char));
    if (!options_fp) {
        return nullptr;
    }
    strncpy(options_fp, uc->path, strlen(uc->path) + 1);
    strncat(options_fp, "/options", strlen("/options") + 1);

    UseCaseOptions *opts = malloc(sizeof(UseCaseOptions));
    if (!opts) {
        free(options_fp);
        return nullptr;
    }
    memcpy(opts, &UseCaseOptionsDefault, sizeof(UseCaseOptionsDefault));

    int fd = open(options_fp, O_RDONLY);
    error_t e = errno;
    free(options_fp);
    if (fd == -1 && errno != ENOENT) {
        errno = e;
        free(opts);
        return nullptr;
    }
    if (fd == -1) {
        errno = ENOENT;
        return opts;
    }

    ssize_t buf_cap = 128;
    char *buf = calloc(buf_cap, sizeof(char));
    if (!buf) {
        free(opts);
        return nullptr;
    }
    ssize_t total_read = 0;
    ssize_t b = 0;
    while ((b = read(fd, buf + total_read, buf_cap - total_read - 1))) {
        total_read += b;
        if (total_read < buf_cap - 1) {
            continue;
        }
        buf_cap *= 2;
        char *new_buf = realloc(buf, buf_cap * sizeof(char));
        if (!new_buf) {
            free(buf);
            free(opts);
            return nullptr;
        }
        buf = new_buf;
    }

    char *c = strtok(buf, "\n");
    while (c) {
        char *opt_name = strsep(&c, "=");
        if (!opt_name) {
            goto _continue;
        }
        char *opt_value = strsep(&c, "=");
        if (!opt_value) {
            goto _continue;
        }

        // TODO: improve this
        if (strcmp(opt_name, "no-stdin") == 0) {
            if (strcmp(opt_value, "true") == 0) {
                opts->no_stdin = true;
            }
        }
        if (strcmp(opt_name, "no-stdout") == 0) {
            if (strcmp(opt_value, "true") == 0) {
                opts->no_stdout = true;
            }
        }
        if (strcmp(opt_name, "no-stderr") == 0) {
            if (strcmp(opt_value, "true") == 0) {
                opts->no_stderr = true;
            }
        }
        if (strcmp(opt_name, "skip") == 0) {
            if (strcmp(opt_value, "true") == 0) {
                opts->skip = true;
            }
        }
    _continue:
        c = strtok(nullptr, "\n");
    }

    free(buf);
    return opts;
}

UseCase *uc_init(const char *name, const char *base_path) {
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

    uc->opts = uc_opts(uc);
    if (!uc->opts) {
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

UseCase **ucs_read(const char *path_to_use_cases) {
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

    size_t expected_size = 4;
    char *expected = calloc(4, expected_size);
    ssize_t content = read(fd, expected, expected_size - 1);
    if (content < 1) {
        close(fd);
        return nullptr;
    }
    expected[content - 1] = '\0';
    close(fd);

    char *actual = nullptr;
    bool pass = r.exit_code == atoi(expected);
    if (!pass) {
        int n = snprintf(actual, 0, "%d", r.exit_code);
        if (n < 0) {
            free(expected);
            return nullptr;
        }
        actual = calloc(++n, sizeof(char));
        if (!actual) {
            free(expected);
            return nullptr;
        }
        n = snprintf(actual, n, "%d", r.exit_code);
        if (n < 0) {
            free(actual);
            free(expected);
            return nullptr;
        }
    }

    AssertionResult *as = calloc(1, sizeof(AssertionResult));
    if (!as) {
        return nullptr;
    }
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
    if (uc->opts) {
        free((void *)uc->opts);
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
