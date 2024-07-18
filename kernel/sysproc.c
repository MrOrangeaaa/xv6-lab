#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
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


#ifdef LAB_PGTBL
/**
 * 注意：用户必须保证，从传入的虚拟地址base开始，往后连续len*PGSIZE个虚拟地址都是有效的（“有效的” 即 “在页表中映射过的”）
 */
int
sys_pgaccess(void)
{
  pagetable_t pagetable = myproc()->pagetable;
  uint64 base, mask;
  int len;

  if(argaddr(0, &base) < 0)
    return -1;
  if(argint(1, &len) < 0)
    return -1;
  if(argaddr(2, &mask) < 0)
    return -1;

  if(len > 64)  //由于掩码位数有限，故一次最多检查64个PTE -> 即64个内存页
  {
    printf("len is too long\n");
    return -1;
  }
  
  uint64 mask_temp = 0;
  uint8 pte_cnt = 0;
  pte_t* cur_pte = walk(pagetable, base, 0);
  if(cur_pte == 0)
    panic("sys_pgaccess: walk");
  uint16 cur_idx = (base >> 12) & 0x1FF;
  while(pte_cnt < len)
  {
    if((*cur_pte & PTE_V) == 0)
      panic("sys_pgaccess: va not mapped");
    if(*cur_pte & PTE_A)
    {
        mask_temp |= (1 << pte_cnt);
        *cur_pte ^= PTE_A;  //clear PTE_A bit.
    }
    pte_cnt++;
    if(cur_idx < 511)
    {
      cur_pte++;
      cur_idx++;
    }else {  //已经到达当前page table的末尾
      base += pte_cnt * PGSIZE;
      if((cur_pte = walk(pagetable, base, 0)) == 0)
        panic("sys_pgaccess: walk");
      cur_idx = 0;
    }
  }

  if(copyout(pagetable, mask, (char*)&mask_temp, sizeof(uint64)) < 0)
    return -1;

  return 0;
}
#endif