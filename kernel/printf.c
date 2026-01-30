//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;  // 是否启用锁
} pr;

static char digits[] = "0123456789abcdef";  // 作为查找表，用于将数值转换为对应的ASCLL字符。

static void printint(int xx, int base, int sign) {
  // 将一个整数xx按照指定的进制base打印出来。
  // 如果sign>0且xx为负数，则打印负号。
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void printptr(uint64 x) {
  // 打印一个64位的无符号整数（指针/地址），格式为16进制
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void printf(char *fmt, ...) {
  // fmt: 格式字符串，类似C语言标准库中的printf函数
  va_list ap;  // 可变参数列表
  int i, c, locking;  // locking: 是否启用锁
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if(fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);  // 初始化ap，使其指向fmt的第一个可变参数
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);  // va_arg(ap, type): 从参数列表中获取下一个参数，并将其视为type类型
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&pr.lock);
}

void
panic(char *s)
{
  pr.locking = 0;  // 禁用锁，以防止死锁
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // freeze uart output from other CPUs
  for(;;)  // 让系统停在这里，死机状态，等待重启
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}
