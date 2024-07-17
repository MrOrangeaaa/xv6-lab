// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

#ifdef LAB_NET
#define E1000_IRQ 33
#endif

// core local interruptor (CLINT), which contains the timer.
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot.

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)  //内存(DRAM)大小为128M

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - (p)*2*PGSIZE - 3*PGSIZE)


/**
 * User memory layout.
 * Address zero first:
 *   text
 *   original data and bss
 *   fixed-size stack
 *   expandable heap
 *   ...
 *   USYSCALL (shared with kernel)
 *   TRAPFRAME (p->trapframe, used by the trampoline)
 *   TRAMPOLINE (the same page as in the kernel)
 */
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

#ifdef LAB_PGTBL
#define USYSCALL (TRAPFRAME - PGSIZE)
#endif

#ifdef LAB_PGTBL
/**
 * 共享页 -> usyscall page
 * 通常，为加速某些系统调用，内核每创建一个用户进程，就会额外分配出一个物理内存页，作为"usyscall page"
 * 这块内存是内核与用户进程共享的(也就是说，这个page也会被映射到用户进程的页表中)，但用户进程只有读权限
 * 内核会把一些数据分享在这里，这样用户进程想要获取某些数据的时候就无须在用户态和内核态之间来回切换，直接读这个page就行了
 * 这个page目前就只存放如下这样一个结构体：
 */
struct usyscall {
  int pid;  // Process ID
};
#endif