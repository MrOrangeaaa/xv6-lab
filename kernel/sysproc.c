#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

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
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
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


// my func1
uint64
sys_trace(void)
{
  int mask;
  if(argint(0, &mask) < 0)  //限定有效参数范围：[0, 2147483647]
    return -1;
  myproc()->trace_mask = mask;
  return 0;
}

// my func2
uint64
sys_sysinfo(void)
{
  //获取用户态的sysinfo地址 -> 这个地址是基于用户态页表的虚拟地址
  uint64 user_infoaddr;
  if(argaddr(0, &user_infoaddr) < 0)
    return -1;
  
  //获取当前(用户)进程
  struct proc *p = myproc();
  
  //获取系统信息
  struct sysinfo info;
  info.freemem = get_freemem();
  info.nproc = get_usedproc();

  //Copy from kernel to user.
  if(copyout(p->pagetable, user_infoaddr, (char*)&info, sizeof(info)) < 0)
      return -1;

  return 0;
}