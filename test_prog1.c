#include <stdio.h>
#include <unistd.h>

int main() {
    int count = 0;
    while (1) {
        printf("Test program 1: running, count=%d\n", count++);
        fflush(stdout);
        sleep(5);
    }
    return 0;
}