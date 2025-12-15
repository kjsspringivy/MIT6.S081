// Saved registers for kernel context switches. 保存内核上下文切换所需的寄存器。
struct context {
  uint64 ra;  // return address 返回地址
  uint64 sp;  // stack pointer 栈指针

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// the sscratch register points here.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table 内核页表指针
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack 内核栈顶指针
  /*  16 */ uint64 kernel_trap;   // usertrap() 内核中断处理函数地址
  /*  24 */ uint64 epc;           // saved user program counter 记录了用户程序被中断时正在执行的那条指令的地址
  /*  32 */ uint64 kernel_hartid; // saved kernel tp 内核 CPU ID
  /*  40 */ uint64 ra;  // 子程序执行完返回父程序的地址
  /*  48 */ uint64 sp;  // 用户栈指针，指向用户栈顶
  /*  56 */ uint64 gp;  // 全局指针，指向全局数据区
  /*  64 */ uint64 tp;  // 线程指针，指向当前线程的局部存储区
  /*  72 */ uint64 t0;  // 临时寄存器，用于函数调用过程中保存临时数据 (t0-t6)
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;  // 保存寄存器，用于保存函数调用过程中需要保留的数据 (s0-s11) 这些寄存器在函数调用前后必须保持不变
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;  // 函数参数/返回值寄存器 (a0-a7)
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;  // 进程锁（自旋锁）

  // p->lock must be held when using these:
  enum procstate state;        // Process state  进程状态
  void *chan;                  // If non-zero, sleeping on chan  睡眠通道，当处于 SLEEPING状态时，这里记录了正在等待的对象。用于睡眠唤醒查询
  int killed;                  // If non-zero, have been killed  进程是否被杀死的标志
  int xstate;                  // Exit status to be returned to parent's wait  退出状态码，父进程通过 wait 系统调用获取
  int pid;                     // Process ID  进程号

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process  指向父进程的指针。子进程退出时，需要用该指针找到父进程并唤醒它

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack 内核栈的虚拟地址
  uint64 sz;                   // Size of process memory (bytes)  进程内存大小（以字节为单位）
  pagetable_t pagetable;       // User page table  用户页表，定义了当前进程的用户虚拟地址到物理内存的映射
  struct trapframe *trapframe; // data page for trampoline.S 用户寄存器保存区
  struct context context;      // swtch() here to run process  内核上下文，保存内核线程的寄存器
  struct file *ofile[NOFILE];  // Open files 打开文件描述符表
  struct inode *cwd;           // Current directory  当前工作目录，解析相对路径时用
  char name[16];               // Process name (debugging)  进程名，用于调试和打印日志时分辨是哪个进程。
};
