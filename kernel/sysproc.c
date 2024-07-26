#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  
  /**
   * proc结构体中的sz字段记录了内核实际分配给当前用户进程的内存大小(bytes) -> trampoline和trapframe不算在内
   * 所以不妨将其理解为一个指针值，指向heap的上边界
   */
  int addr;
  addr = myproc()->sz;
  // printf("sz = %p\n", addr);

  if(n < 0)  //用户进程申请释放内存 -> 释放可就不需要“懒释放”，赶紧趁早释放掉...
  {
    if(addr + n < 0)
      return -1;
    
    if(growproc(n) < 0)
      return -1;
  }
  else  //用户进程申请分配内存 -> 懒分配
  {
    myproc()->sz += n;
  }

  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
