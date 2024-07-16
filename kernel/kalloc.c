// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

/**
 * first address after kernel.
 * defined by kernel.ld.
 */
extern char end[];

/**
 * 表示一个空闲内存页
 */
struct run {
  struct run *next;
};

/**
 * 维护了一个全局变量kmem
 * 它是一个空闲页链表(free page list)，链表中的每个结点均指向一个“可用的内存页(free page)”
 */
struct {
  struct spinlock lock;  //自旋锁，用于保护空闲内存页链表的并发访问
  struct run *freelist;  //链表头指针，初始化为NULL
}kmem;

/**
 * 初始化内存分配器
 */
void
kinit()
{
  //初始化自旋锁
  initlock(&kmem.lock, "kmem");
  //释放从内核结束到物理内存顶端的所有内存区域
  freerange(end, (void*)PHYSTOP);

  printf("free memory range: [0x%016llx ~ 0x%016llx]\n", (unsigned long long)end, (unsigned long long)PHYSTOP);
}

/**
 * 用于释放某一段内存区域，并将其分成多个页，加入空闲页链表
 */
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

/**
 * 释放一个物理内存页
 * Free the page of physical memory pointed at by v,
 * which normally should have been returned by a call to kalloc().
 * (The exception is when initializing the allocator; see kinit above.)
 */
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // fill with junk to catch dangling refs. -> 用垃圾数据填充内存页，以捕捉悬挂引用
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //将内存页加入空闲页链表(头插)
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

/**
 * 分配一个物理内存页
 * Allocate one 4096-byte page of physical memory.
 * Returns a pointer that the kernel can use.
 * Returns 0 if the memory cannot be allocated.
 */
void *
kalloc(void)
{
  struct run *r;

  //从空闲页链表中取出一个空闲页(从链表头部取)
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;  //更新链表头指针
  release(&kmem.lock);

  //用垃圾数据初始化填充取出的内存页
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
