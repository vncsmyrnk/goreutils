#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUF_SIZE 1024

ssize_t print_fd(int fd, size_t buf_size) {
    char c[buf_size];
    ssize_t b;

    while(1) {
        b = read(fd, &c, BUF_SIZE);
        if (b < 1) {
            break;
        }
        c[b] = '\0';
        printf("%s", c);
    }
    return b;
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        if (print_fd(fd, BUF_SIZE) == -1) {
            perror("read");
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    if (i > 1) {
        return 0;
    }

    if (print_fd(STDIN_FILENO, BUF_SIZE) == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    return 0;
}
