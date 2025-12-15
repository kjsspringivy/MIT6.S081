#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number  主设备类型数量
#define ROOTDEV       1  // device number of file system root disk  根文件系统所在的设备号
#define MAXARG       32  // max exec arguments exec 系统调用的最大参数个数
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes 单个文件系统操作最大写入块数（每一种系统调用涉及的磁盘块修改不超过10块）
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log  日志中最大数据块数
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache  磁盘块缓存的大小。
#define FSSIZE       1000  // size of file system in blocks 单位为块（xv6中一块为1024字节），生成的 fs.img 镜像的大小。
#define MAXPATH      128   // maximum file path name
