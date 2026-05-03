#include <stdio.h>
#include <unistd.h>

int main() {
    int count = 0;
    while (1) {
        printf("Test program 2: running, count=%d\n", count++);
        fflush(stdout);
        sleep(7);
    }
    return 0;
}