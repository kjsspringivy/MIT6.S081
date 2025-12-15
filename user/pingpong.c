#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }

    int pc[2]; 
    pipe(pc);
    int cp[2];
    pipe(cp);
    char buf;

    int child_pid = fork();
    
    if (child_pid == 0) {
        int cur_pid = getpid(); 
        if (read(pc[0], &buf, 1) != 1) {
            fprintf(2, "Child %d: read error\n", cur_pid);
            exit(1);
        }
        else {
            printf("%d: received ping\n", cur_pid);
        }

        if (write(cp[1], &buf, 1) != 1) {
            fprintf(2, "Child %d: write error\n", cur_pid);
            exit(1);
        }
        exit(0);
    }
    else if (child_pid > 0) {
        int parent_pid = getpid();
        if (write(pc[1], "x", 1) != 1) {
            fprintf(2, "Parent %d: write error\n", parent_pid);
            exit(1);
        }
        
        if (read(cp[0], &buf, 1) != 1) {
            fprintf(2, "Parent %d: read error\n", parent_pid);
            exit(1);
        }
        else {
            printf("%d: received pong\n", parent_pid);
        }
        exit(0);
    }

}
