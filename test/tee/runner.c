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
    char *path;
    char **argv;
} UseCase;

ssize_t uc_input_fd(UseCase *uc) {
    char *input_path =
        calloc(strlen(uc->path) + strlen("/input") + 1, sizeof(*uc->path));
    if (input_path == NULL) {
        return -1;
    }
    strcpy(input_path, uc->path);
    strcat(input_path, "/input");
    int fd = open(input_path, O_RDONLY);
    free(input_path);
    return fd;
}

ssize_t uc_argv(UseCase *uc) {
    char *argv_path =
        calloc(strlen(uc->path) + strlen("/argv") + 1, sizeof(*uc->path));
    if (argv_path == NULL) {
        return -1;
    }
    strcpy(argv_path, uc->path);
    strcat(argv_path, "/argv");
    int fd = open(argv_path, O_RDONLY);
    free(argv_path);
    if (fd == -1) {
        return fd;
    }

    char buf[1024], *argv = NULL;
    int buf_count = 0;
    ssize_t b;
    while (1) {
        b = read(fd, buf, sizeof(buf));
        if (b < 1) {
            break;
        }
        buf[b] = '\0';
        argv = reallocarray(argv, ++buf_count * b, sizeof(buf));
        if (argv == NULL) {
            return -1;
        }
        strcpy(argv, buf);
    }
    if (b == -1) {
        return b;
    }

    uc->argv = calloc(1, sizeof(*uc->argv));
    uc->argv[0] = ""; // first argument is the program's name

    int argc = 1;
    char *tok;
    for (tok = strtok(argv, "\n"); tok != NULL; tok = strtok(NULL, "\n")) {
        uc->argv = reallocarray(uc->argv, argc + 1, sizeof(*uc->argv));
        if (uc->argv == NULL) {
            return 1;
        }
        uc->argv[argc++] = tok;
    }
    uc->argv[argc] = NULL;

    return b;
}

// ssize_t uc_assert(UseCase *uc, bool *result, char *wd) {
//   char *input_path =
//       calloc(strlen(uc->path) + strlen("/input") + 1, sizeof(*uc->path));
//   if (input_path == NULL) {
//     return -1;
//   }
//   strcpy(input_path, uc->path);
//   strcat(input_path, "/input");
//   printf("input_path: %s\n", input_path);
//   int fd = open(input_path, O_RDONLY);
//   if (fd == -1) {
//     free(input_path);
//     return fd;
//   }
//   close(fd);
//   free(input_path);
//   return true;
// }

// WARN: does not free itself
void uc_free(UseCase *uc) {
    if (uc == NULL)
        return;
    if (uc->path != NULL)
        free(uc->path);
    if (uc->argv != NULL) {
        char *arg = uc->argv[0];
        while (arg != NULL)
            free(arg++);
        free(uc->argv);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        exit(EXIT_FAILURE);
    }
    char *p_uc = argv[1];
    char *p_bin = argv[2];

    char tmp_path[] = "/tmp/goreutils-tee-test.XXXXXX";
    if (mkdtemp(tmp_path) == NULL) {
        perror("mkdtemp");
        exit(EXIT_FAILURE);
    }

    char *p_bin_fp = realpath(p_bin, NULL);
    if (p_bin_fp == NULL) {
        perror("realpath");
        exit(EXIT_FAILURE);
    }

    DIR *dp = opendir(p_uc);
    if (dp == NULL) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    int i = 0;
    UseCase *ucs = NULL;
    struct dirent *ep;
    while ((ep = readdir(dp))) {
        if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0) {
            continue;
        }
        ucs = reallocarray(ucs, i + 1, sizeof(UseCase));
        if (ucs == NULL) {
            perror("reallocarray");
            exit(EXIT_FAILURE);
        }
        char *uc_path =
            calloc(strlen(ep->d_name) + strlen(p_uc) + 2, sizeof(*ep->d_name));
        if (uc_path == NULL) {
            perror("calloc");
            free(ucs);
            exit(EXIT_FAILURE);
        }
        strcpy(uc_path, p_uc);
        strcat(uc_path, "/");
        strcat(uc_path, ep->d_name);
        UseCase uc = {.path = uc_path};
        ucs[i++] = uc;
    }
    closedir(dp);
    int uc_count = i;

    for (i = 0; i < uc_count; i++) {
        UseCase *uc = &ucs[i];
        pid_t pid = fork();
        switch (pid) {
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
            break;
        case 0:
            int input_fd = uc_input_fd(uc);
            if (input_fd == -1) {
                perror("uc_input_fd");
                exit(EXIT_FAILURE);
            }
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(input_fd);

            if (uc_argv(uc) == -1) {
                perror("uc_argv");
                exit(EXIT_FAILURE);
            }

            if (chdir(tmp_path) == -1) {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            execv(p_bin_fp, uc->argv);
            perror("execv");
            exit(EXIT_FAILURE);
            break;
        default:
            int status;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                exit(EXIT_FAILURE);
            }
        }
    }

    for (i = 0; i < uc_count; i++) {
        uc_free(ucs + i);
    }
    free(ucs);
    free(p_bin_fp);
    return 0;
}
