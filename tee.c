#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 1024

void usage(FILE *f, char *arg0) {
    fprintf(f, "usage: %s [OPTIONS]... [FILE]...\n"
               "\nOptions:\n"
               "  --help  display this help message\n", arg0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr, argv[0]);
        exit(EXIT_FAILURE);
    }

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        } else if (argv[i][0] == '-') {
            usage(stderr, argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    int *fds = calloc(argc, sizeof(int));
    if (fds == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    int fcount = 0;
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_TRUNC);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        fds[i-1] = fd;
        fcount++;
    }

    char buf[BUF_SIZE];
    ssize_t b;
    while (1) {
        b = read(STDIN_FILENO, buf, sizeof(buf));
        if (b < 1) {
            break;
        }
        buf[b] = '\0';
        printf("%s", buf);
        for (i = 0; i < fcount; i++) {
            if (write(fds[i], buf, strlen(buf)) == -1) {
                perror("write");
                exit(EXIT_FAILURE);
            }
        }
    }

    for (i = 0; i < fcount; i++) {
        close(fds[i]);
    }
    free(fds);
    return 0;
}
