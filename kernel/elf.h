// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];  // 包含与机器无关的格式信息：位数32/64，大端序/小端序，版本号，ABI等
  ushort type;  // 没链接好的可重定位文件.o/可执行文件/共享对象.so
  ushort machine;  // 目标机器架构
  uint version;  // ELF文件版本
  uint64 entry;  // 程序入口地址, 程序开始执行的第一条指令的虚拟地址
  uint flags;  // 与处理器相关的标志
  ushort ehsize;  // ELF文件头的大小
  uint64 phoff;  // 程序头表在文件中的起始偏移量
  ushort phentsize;  // 一个程序头表的的大小
  ushort phnum;  // 程序头表中条目的数量
  uint64 shoff;  // 节头表在文件中的偏移量
  ushort shentsize;  // 一个节头表的大小
  ushort shnum;  // 节头表中条目的数量
  ushort shstrndx;  // 节头表中保存节名称的节的索引
};

// Program section header
struct proghdr {
  uint32 type;    // 是否为需要加载到内存的段
  uint32 flags;   // 段的标志位(可读/可写/可执行)
  uint64 off;     // 段在ELF文件中的偏移量
  uint64 vaddr;   // 段需要被加载到的内存虚拟地址
  uint64 paddr;   // 物理地址(未使用)
  uint64 filesz;  // 段在ELF文件中占多少字节
  uint64 memsz;   // 段在内存中的需要占多少字节 >= filesz
  uint64 align;   // 对齐方式
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1  // 需要加载到内存的段

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
