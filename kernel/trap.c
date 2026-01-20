#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;  // 保护ticks，避免多个CPU同时读写
uint ticks;  // 全局计数器 （时钟中断到来一次+1）

// 把符号当地址用，不代表真实数据内容，只是为了获取它们的地址（内核虚拟地址）
// trampoline: trampoline.S 的起始地址 内核va
// uservec: trampoline.S 中 uservec 函数的地址 内核va
// userret: trampoline.S 中 userret 函数的地址 内核va
extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
// 当做可执行函数用
void kernelvec();

extern int devintr();  // 设备中断处理函数

// 全局陷阱子系统初始化（一次性）
void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
// 将每个 CPU 的陷阱入口地址（stvec）设置为 kernelvec
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
void
usertrap(void)
{
  int which_dev = 0;  // 用于标记是哪种设备中断（devintr的返回值）0:未知 1:其他外设 2:定时器中断
  // 确认trap确实来自用户态
  if((r_sstatus() & SSTATUS_SPP) != 0)  // Trap前的状态, 0:user, 1:supervisor
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // stvec 寄存器设置为 kernelvec 的地址
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 保存user pc
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // 允许中断
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    if (p->alarm_interval !=0){
      p->ticks_counter++;
      if (p->ticks_counter >= p->alarm_interval){
        // 时间到，触发用户态 alarm handler
        if (p->in_handler == 0){  // 非重入状态才处理
          p->in_handler = 1;
          // 保存当前用户态 trapframe 到 alarm_trapframe
          *(p->alarm_trapframe) = *(p->trapframe);
          // 设置用户态 pc 到 alarm_handler
          p->trapframe->epc = p->alarm_handler;
          p->ticks_counter = 0;
        }
      }
    }
    yield();
  }

  usertrapret();
}

// return to user space
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // stvec 切换回 uservec
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  // 从用户态进内核时需要的的内核环境参数：内核页表、内核栈、要跳的C入口、当前 hartid。
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;  // 内核页表下的usertrap函数地址
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  // 归位sstatus寄存器，防止嵌套导致的参数错误
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode 回归为用户态
  x |= SSTATUS_SPIE; // enable interrupts in user mode 允许中断
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // 将 sepc 设置为先前保存的用户 PC
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  // 准备用户页表
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 计算 userret 的固定映射地址并“当函数调用”跳过去
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  // 进入kerneltrap时中断必须是关闭的
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  // 如果是时钟中断(时间片用完)，则让出CPU
  // 确保CPU确实绑定了某个进程（不是早期启动或纯调度器上下文），且进程正在运行
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();
  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr() { 
  // 时钟计数 
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);  // 唤醒等待ticks变化的进程,检查是否有进程的时间到了，唤醒它们起来检查是否满足醒来条件
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr() {
  // 中断处理函数
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L)  // 最高位为1 表示是中断
      && (scause & 0xff) == 9){  // 硬件设备中断
    // this is a supervisor external interrupt, via PLIC.
    // irq indicates which device interrupted.
    int irq = plic_claim();  // 从PLIC中获取中断号,表示是哪个设备发起的中断

    if(irq == UART0_IRQ){  // 串口中断(如键盘输入)
      uartintr();
    } else if(irq == VIRTIO0_IRQ){  // 磁盘中断(如读写完成)
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);  // 通知PLIC该中断已处理完毕，可以接收新的中断请求
    return 1;

  } else if(scause == 0x8000000000000001L){  // 时钟中断
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.
    // xv6 中，物理时钟中断发生在 M 模式（Machine mode）。
    // M 模式的 timer handler 会把中断转换成一个 S 模式软件中断（SSIP）转发给 S 模式内核。
    // 所以这里看到的是 Software Interrupt。

    if(cpuid() == 0){  // CPU 0 负责维护全局时钟 ticks，避免多个 CPU 都去加锁争抢更新同一个全局变量。
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);  // 清除 SSIP 位，表示已处理该软件中断

    return 2;
  } else {
    return 0;
  }
}

