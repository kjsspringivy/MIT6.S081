#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"


int main(int argc, char *argv[]){
    if (argc<2) {
        fprintf(2, "Usage: xargs command [args...]\n");
        exit(1);
    }
    char *xargs_argv[MAXARG];
    int index = 0;
    for(int i=1; i<argc; i++){
        xargs_argv[index++] = argv[i];
    }
    
    char buf[512]; // 存储从标准输入读取的内容
    char *p = buf;
    char ch;  // 存储每次读取的字符

    while (read(0, &ch, 1) > 0) {
        if (ch == '\n') {
            *p = 0;
            int cur = index;
            xargs_argv[cur++] = buf;
            for (char *k=buf; *k!=0; k++){
                if (*k == ' '){
                    *k = 0;
                    xargs_argv[cur++] = k+1;
                }
            }
            xargs_argv[cur] = 0;
            if (fork() == 0) {
                exec(xargs_argv[0], xargs_argv);
                fprintf(2, "xargs: exec %s failed\n", xargs_argv[0]);
                exit(1);
            }
            wait(0);
            p = buf;

        }
        else {
            *p++ = ch;
        }
        
    }
    exit(0);
}