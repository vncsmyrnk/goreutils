#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUF_SIZE 1024

int main(void) {
    char c[BUF_SIZE];
    ssize_t b;

    while(1) {
        b = read(STDIN_FILENO, &c, BUF_SIZE);
        if (b < 1) {
            break;
        }
        c[b] = '\0';
        printf("%s", c);
    }

    if (b == -1) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    return 0;
}
