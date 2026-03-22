struct file {
#ifdef LAB_NET
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_SOCK } type;
#else
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
#endif
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
#ifdef LAB_NET
  struct sock *sock; // FD_SOCK
#endif
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number 设备号：文件存在哪个磁盘上
  uint inum;          // Inode number inode编号：文件在磁盘inode数组上的索引号
  int ref;            // Reference count 内存引用计数：当前内存有多少个指针指向该inode
  struct sleeplock lock; // protects everything below here 保护下面的所有字段
  int valid;          // inode has been read from disk? 是否已经从磁盘读入内存

  short type;         // 文件类型：普通文件、目录、设备、空
  short major;        // 主设备号：文件是T_DEVICE时有效，表示设备类型
  short minor;        // 次设备号：文件是T_DEVICE时有效，区分同类型下的不同子设备表示设备编号
  short nlink;        // 代表有几个“文件名”指向它
  uint size;          // 文件大小，以字节为单位
  uint addrs[NDIRECT+1];  // 该文件实际数据所在的磁盘块号
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
#define STATS   2
