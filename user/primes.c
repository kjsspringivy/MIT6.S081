#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void child_process(int p[2]);


int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(2, "Usage: primes\n");
        exit(1);
    }
    int p[2];
    pipe(p);
    int pid = fork();
    if (pid == 0) {
        child_process(p);
    }
    else if (pid > 0) {
        close(p[0]);
        for (int i=2; i<=35; i++) {
            if (i == 2) {
                printf("prime %d\n", i);
            }
            else if (i % 2 != 0) {
                write(p[1], &i, sizeof(i));
            }
        }
        close(p[1]);
        wait(0);
        exit(0);
    }
}

void child_process(int p[2]) {
    close(p[1]);
    int prime;
    if (read(p[0], &prime, sizeof(prime)) <= 0) {
        exit(0);
    }
    printf("prime %d\n", prime);

    int p_new[2];
    pipe(p_new);
    int pid = fork();
    if (pid == 0) {
        child_process(p_new);
    }
    else if (pid > 0) {
        int num;
        while (read(p[0], &num, sizeof(num)) > 0) {
            if (num % prime != 0) {
                write(p_new[1], &num, sizeof(num));
            }
        }
        close(p[0]);
        close(p_new[1]);
        wait(0);
        exit(0);
    }
}