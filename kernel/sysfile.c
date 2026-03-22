//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf) {
  // 判断第n个参数是不是合法文件描述符
  // n:系统调用的第几个参数
  // pfd:文件描述符指针，返回给调用者
  // pf:文件结构体二级指针，返回给调用者
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f) {
  // 为给定的文件分配一个文件描述符,返回文件描述符/-1
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64 sys_dup(void) {
  // dup 系统调用, 复制一个现有的文件描述符
  // 调用 dup(3) 会返回一个新的文件描述符（比如 fd = 4），并且这两个描述符指向的是底层的同一个文件，共享相同的读取偏移量（offset）和权限。
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);  // 增加f引用计数
  return fd;
}

uint64 sys_read(void) {
  // read系统调用 read(fd, addr, n)
  struct file *f;  // 文件指针
  int n;  // 读取字节数
  uint64 p;  // 用户地址

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64 sys_write(void) {
  // write系统调用 write(fd, addr, n)
  struct file *f;  // 文件指针
  int n;  // 写入字节数
  uint64 p;  // 用户地址

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64 sys_close(void) {
  // close系统调用 close(fd)
  int fd;  // 文件描述符
  struct file *f;  // 

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64 sys_fstat(void) {
  // fstat系统调用 fstat(fd, addr)
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void) {
  // link系统调用 link(oldpath, newpath),非目录
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;  // ip: old对应的inode，dp: new路径的父目录对应的inode

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();  // 创建硬链接会修改磁盘，需要事务
  // 读取old的inode
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  // 读取new的父目录inode
  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){  // 禁止跨设备创建硬链接
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp) {
  // 判断目录是否为空（除了 . 和 .. 以外没有其他目录项）
  // return 1:空；0:非空
  // 需要调用者持有 dp->lock 睡眠锁
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 sys_unlink(void) {
  // unlink系统调用 unlink(path)，非空目录
  struct inode *ip, *dp;  // ip: path的inode，dp: path父目录的inode
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;  // name在path中的字节偏移

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){  // 非空目录不能unlink
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));  // 组装一个全为 0 的空白目录项
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))  // 写入空白de
    panic("unlink: writei");
  if(ip->type == T_DIR){  // 如果被unlink的是目录，意味着原先的目标..记录销毁了，父目录的链接数要减1
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;  // 无论是否目录，目标inode的链接数都要减1
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode* create(char *path, short type, short major, short minor) {
  // 内核函数（由上层函数将其包含在事务中），创建文件/目录/设备文件，在磁盘创建一个inode
  // path:文件路径；type:文件类型；major, minor:设备号，仅type为T_DEVICE时有效
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)  // 获取父目录inode
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){  // 文件已存在，返回
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)  // 在磁盘上分配一个新的inode
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64 sys_open(void) {
  // open系统调用 open(path, omode)，omode: O_RDONLY,O_WRONLY,O_RDWR,O_CREATE,O_TRUNC
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);  // open + O_CREATE 单指创建文件，创建目录（mkdir），创建设备文件（mknod）
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){  // 文件必须已存在
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){  // 目录只能以只读打开
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){  // 设备文件校验
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){  // 分配内核文件对象与文件描述符
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){ // 重定向，清空原inode内容
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64 sys_mkdir(void) {
  // 创建目录 mkdir(path)
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_mknod(void) {
  // 创建设备文件 mknod(path, major, minor)
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_chdir(void) {
  // 切换当前工作目录 chdir(path)，类似于cd path
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){  // path 必须是目录
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64 sys_exec(void) {
  // exec系统调用 exec(path, argv)，path:可执行文件路径，argv:参数字符串数组
  char path[MAXPATH], *argv[MAXARG];
  int i;  // 第几个参数
  uint64 uargv, uarg; // uargv: 用户态传入的参数字符串指针数组的起始地址, uarg:每个arg的地址

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){  // 参数量超限
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){  // 获取第i个参数的地址失败
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();  // 为第i个参数一页物理内存
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)  // 从用户地址uarg获取参数字符串到内核地址argv[i]，失败则返回-1
      goto bad;
  }

  int ret = exec(path, argv);  // 调用exec函数执行可执行文件，成功返回0，失败返回-1

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)  // 清理内存
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64 sys_pipe(void) {
  // pipe系统调用 pipe(fdarray)，fdarray:用户地址，存返回的两个文件描述符，分别指向管道的读端和写端
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)  // 分配管道的读端和写端文件结构体
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){  // 为读端和写端分配文件描述符
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}


#ifdef LAB_NET
int
sys_connect(void)
{
  struct file *f;
  int fd;
  uint32 raddr;
  uint32 rport;
  uint32 lport;

  if (argint(0, (int*)&raddr) < 0 ||
      argint(1, (int*)&lport) < 0 ||
      argint(2, (int*)&rport) < 0) {
    return -1;
  }

  if(sockalloc(&f, raddr, lport, rport) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0){
    fileclose(f);
    return -1;
  }

  return fd;
}
#endif
