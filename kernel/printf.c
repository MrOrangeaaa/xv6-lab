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
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
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

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
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
      printint(va_arg(ap, int), 10, 1);
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
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}


void 
backtrace(void)
{
  printf("backtrace:\n");

  uint64* cur_frame = (uint64*)r_fp();  //获取当前frame pointer
  uint64* stack_bottom = (uint64*)PGROUNDUP((uint64)cur_frame);  //栈底为高地址
  uint64* stack_top = (uint64*)PGROUNDDOWN((uint64)cur_frame);  //栈顶为低地址

  /**
   * Xv6 allocates one page for each stack in the xv6 kernel at PAGE-aligned address.
   * xv6只给每个用户进程分配1个page用作栈区，并且栈区向低地址增长
   * 所以每个用户进程调用的第一个函数，其栈帧的帧指针(即首个frame pointer)一定是指向 <此页的最高地址 + 8 = 下一页的首地址> 这个位置
   * 故我们不妨借此特性，用于终止循环
   */
  //遍历当前用户进程的stack(中的所有previous frame pointer)
  while(cur_frame >= stack_top && cur_frame < stack_bottom)
  {
    printf("%p\n", *(cur_frame - 1));  //打印当前栈帧的return address
    cur_frame = (uint64*)(*(cur_frame - 2));
  }

  // printf("first frame pointer: %p\n", cur_frame);
}
