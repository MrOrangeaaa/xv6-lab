// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NSTEAL 64  //maximum number of pages that a cpu hart can steal at a time

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel. -> 即<free memory>的起始地址
                   // defined by kernel.ld.

struct run {
  struct run *next;
};


/*--- old version ---*/
// struct {
//   struct spinlock lock;
//   struct run *freelist;
// } kmem;
// void
// kinit()
// {
//   initlock(&kmem.lock, "kmem");
//   freerange(end, (void*)PHYSTOP);
// }

/*--- new version ---*/
struct {
  struct spinlock lock, st_lock;
  struct run *freelist;
  uint64 st_page[NSTEAL];
} kmems[NCPU];  //为每个CPU核心创建一个kmem

const uint8 lk_name_sz = sizeof("kmem_cpu_0");
char kmems_lk_name[NCPU][sizeof("kmem_cpu_0")];

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
  {
    snprintf(kmems_lk_name[i], lk_name_sz, "kmem_cpu_%d", i);
    initlock(&kmems[i].lock, kmems_lk_name[i]);
  }

  freerange(end, (void*)PHYSTOP);  //释放<free memory>
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

/*--- old version ---*/
// void
// kfree(void *pa)
// {
//   struct run *r;

//   if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(pa, 1, PGSIZE);

//   r = (struct run*)pa;

//   acquire(&kmem.lock);
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   release(&kmem.lock);
// }

/*--- new version ---*/
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  // 这里对空闲页的分配策略也很简单，在哪个核心上释放的就分配给哪个核心 -> 有待优化...
  // Disable interrupts to maintain atomicity.
  push_off();
  int cpu = cpuid();
  acquire_without_push(&kmems[cpu].lock);
  r->next = kmems[cpu].freelist;
  kmems[cpu].freelist = r;
  release_without_pop(&kmems[cpu].lock);
  pop_off();
}


// id为cpu的核心偷窃其他核心的空闲页
// 注意遵守一个原则：所有CPU核心都只能为自己偷，不能为别的核心偷
int ksteal(int cpu)
{
  int st_idx = 0;
  uint st_remain = NSTEAL;  //最多还能偷多少页 -> 因为用来装赃物的袋子容量是有限的

  //清空用来装赃物的袋子
  memset(kmems[cpu].st_page, 0, sizeof(kmems[cpu].st_page));

  //遍历所有CPU核心
  for(int i = 0; i < NCPU; i++)
  {
    if(i == cpu) continue;  //不偷自己

    acquire(&kmems[i].lock);
    while(kmems[i].freelist && st_remain)
    {
      kmems[cpu].st_page[st_idx++] = (uint64)kmems[i].freelist;
      kmems[i].freelist = kmems[i].freelist->next;  
      st_remain--;
    }
    release(&kmems[i].lock);

    if(st_remain == 0)  //已达偷窃上限
      break;
  }

  return st_idx;  //返回实际偷到的页数
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.

/*--- old version ---*/
// void *
// kalloc(void)
// {
//   struct run *r;

//   acquire(&kmem.lock);
//   r = kmem.freelist;
//   if(r)
//     kmem.freelist = r->next;
//   release(&kmem.lock);

//   if(r)
//     memset((char*)r, 5, PGSIZE); // fill with junk
//   return (void*)r;
// }

/*--- new version ---*/
void *
kalloc(void)
{
  struct run *r;
  
  // Disable interrupts to maintain atomicity. -> 拒绝抢占式调度
  push_off();
  int cpu = cpuid();
  acquire_without_push(&kmems[cpu].lock);
  r = kmems[cpu].freelist;
  if(r)  //当前CPU有空闲页
  {
    kmems[cpu].freelist = r->next;
    release_without_pop(&kmems[cpu].lock);
  }
  else  //当前CPU没有空闲页了 -> 去偷别人的
  {
    release_without_pop(&kmems[cpu].lock);

    int ret = ksteal(cpu);
    if(ret <= 0)  //一页都没偷到... -> 直接返回空指针(0)
    {
      pop_off();
      return 0;
    }

    acquire_without_push(&kmems[cpu].lock);
    //把偷来的空闲页依次插入当前CPU的freelist（头插）
    for(int i = 0; i < ret; i++)
    {
      if(!kmems[cpu].st_page[i])
        break;
      ((struct run*)kmems[cpu].st_page[i])->next = kmems[cpu].freelist;
      kmems[cpu].freelist = (struct run*)kmems[cpu].st_page[i];
    }

    //现在有空闲页了（偷来的），重新分配一次
    r = kmems[cpu].freelist;
    kmems[cpu].freelist = r->next;
    release_without_pop(&kmems[cpu].lock);
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk  
  return (void*)r;
}