#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *get_filename(char *path) {
    char *p;
    for (p=path+strlen(path); p>= path && *p!='/';p--);
    p++;
    return p;

}


void find(char *path, char *target_name) {
    char *dir_name = get_filename(path);
    if (strcmp(dir_name, target_name) == 0) {
        printf("%s\n", path);
    }
    int fd = open(path, 0);
    char buf[512], *p;
    struct dirent de;
    struct stat st;
    if (fd < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }   
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    if (st.type != T_DIR) {
        close(fd);
        return;
    }
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        close(fd);
        return;
        }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
            continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;
        
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0){
            printf("find: cannot stat %s\n", buf);
            continue;
        }
        find(buf, target_name);
    }
    close(fd);
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: find <path> <file name>\n");
        exit(1);
    }
    
    find(argv[1], argv[2]);
    exit(0);
}