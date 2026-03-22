//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];  // 设备表，记录不同设备的读写函数指针
struct {
  struct spinlock lock;
  struct file file[NFILE];  // 文件包装，可以是管道、设备、inode等
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file* filealloc(void) {
  // 在文件表中寻找并分配一个新的file。
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){  // 引用为0即空闲
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file* filedup(struct file *f) {
  // 增加file的引用计数
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f. (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f) {
  // 关闭文件，减少引用计数，并在引用计数为0时释放资源
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;  // 标记为无效
  release(&ftable.lock);

  if(ff.type == FD_PIPE){ // 如果源文件是管道，调用pipeclose()关闭管道
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
#ifdef LAB_NET
  else if(ff.type == FD_SOCK){
    sockclose(ff.sock);
  }
#endif
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr) {
  // 获取文件元数据，并将其复制到用户地址addr指向的stat结构体中。返回0表示成功，-1表示失败。
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);  // ilock前不需要iget，因为file内部f->ip已经被iget过了
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n) {
  // 从f中读取数据到用户地址addr，返回成功读取的字节数，失败返回-1。
  int r = 0;  // 读取结果，成功为正数，失败为-1

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  }
#ifdef LAB_NET
  else if(f->type == FD_SOCK){
    r = sockread(f->sock, addr, n);
  }
#endif
  else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n) {
  // 向f中写入数据，数据来源用户地址addr，返回成功写入的字节数，失败返回-1。
  int r, ret = 0;  // r:每次写入的字节数，ret:总共成功写入的字节数

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    // -1: 预留一个用来更新文件的inode，-1：预留一个用来更新间接块，-2：预留两个块的slop
    // /2: 磁盘上分配一个数据块，还要更新位图块
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;  // 已经成功写入的字节数
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  }
#ifdef LAB_NET
  else if(f->type == FD_SOCK){
    ret = sockwrite(f->sock, addr, n);
  }
#endif
  else {
    panic("filewrite");
  }

  return ret;
}

