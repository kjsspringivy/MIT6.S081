struct buf {
  int valid;        // 是否已包含有效数据
  int disk;         // 缓存块的数据是否正在“与磁盘交互中”
  uint dev;         // 设备号
  uint blockno;     // 块号 
  struct sleeplock lock;
  uint refcnt;      // 当前有几个内核进程在使用这个块
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

