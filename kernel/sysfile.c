//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}



// Return virtual address of memory-mapped region.
uint64
sys_mmap(void)
{
  uint64 addr;
  uint length;
  int prot, flags, fd, offset;
  struct file *f;

  if(argaddr(0, &addr) < 0 || argint(1, (int*)&length) < 0 || argint(2, &prot) < 0 ||
    argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argint(5, &offset) < 0)
    return -1;
  
  if(length == 0)
    return -1;

  if((offset % PGSIZE) != 0)
    return -1;
  
  // 1. 文件本身不可读，mmap设置了可读； 2. 文件本身不可写，mmap设置了可写，并且还希望将内存映射上的修改写回文件；
  if((!f->readable && (prot & (PROT_READ))) || (!f->writable && (prot & PROT_WRITE) && (flags & MAP_SHARED)))
    return -1;
  
  // length must be page-aligned
  length = PGROUNDUP(length);
  
  /**
   * Tips:
   * 1. mmaptest never pass a non-zero addr argument,
   *    so addr here is ignored and a new unmapped virtual addr is found to map the file.
   * 2. we map files right below where the trapframe is, from high addresses to low addresses.
   * 3. offset must be page-aligned.
   * 4. 实际上这里 "find a new unmapped virtual addr" 的策略是很蠢的，对于已经被释放的高地址视而不见，
   *    这会导致新找的 "va" 只减不增，会逐渐压缩堆区可用的虚拟地址空间，
   *    不过虚拟地址空间往往比物理内存大得多，所以倒是也不会有什么太坏的结果，但总归是不够合理的，有待优化...
   */
  struct proc *p = myproc();
  struct vma *v = 0;
  uint64 va_max = MMAPSTOP;  // va_max: 此虚拟地址往后的内存都已经被使用了 -> 注意，这里是指用户地址空间

  // Find a free vma, and calculate where to map the file along the way.
  for(int i = 0; i < NVMA; i++)
  {
    struct vma *vv = &p->vma[i];
    if(vv->valid == 0)
    {
      if(v == 0)
      {
        v = vv;
        v->valid = 1;
      }
    }
    else if(vv->va < va_max)
    {
      va_max = PGROUNDDOWN(vv->va);
    }
  }
  if(v == 0)
    panic("mmap: no free vma");
  
  v->va = va_max - length;
  v->length = length;
  v->prot = prot;
  v->flags = flags;
  v->f = f;
  v->offset = offset;

  // 将文件的引用计数加1
  filedup(v->f);

  return v->va;
}

static void
munmap_writeback(pagetable_t pagetable, uint64 addr, uint length, struct vma *v)
{
  uint64 start = addr;
  uint64 end = addr + length;
  uint64 va = start;

  while(va < end)
  {
    uint64 next_page_va = (va & ~(PGSIZE - 1)) + PGSIZE;
    uint64 page_end = next_page_va < end ? next_page_va : end;
    uint64 page_length = page_end - va;

    pte_t *pte = walk(pagetable, va, 0);
    if(pte == 0)
    {
      // panic("munmap_writeback: walk");
      va = next_page_va;
      continue;
    }
    if((*pte & PTE_V) == 0)
    {
      // panic("munmap_writeback: not mapped");
      va = next_page_va;
      continue;
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("munmap_writeback: not a leaf");

    // If this page is dirty, we need to write it back to disk.
    if(*pte & PTE_D)
    {
      uint64 pa = PTE2PA(*pte);
      
      // 回盘
      begin_op();
      ilock(v->f->ip);
      writei(v->f->ip, 0, pa, v->offset + (va - v->va), page_length);
      iunlock(v->f->ip);
      end_op();
      
      // Mark the page as clean.
      *pte &= ~PTE_D;
    }
    
    va = next_page_va;
  }
}

// 释放某个内存映射区(memory-mapped region)的全部或者一部分
// 注意：这里是有限制的，要么掐头，要么去尾，要么整体全部释放 -> 不允许在中间挖洞
uint64
munmap(struct proc *p, uint64 addr, uint length)
{
  struct vma *v = get_vma(p, addr);  // 这里保证了addr的合法性，即 v->va <= addr < (v->va + v->length)
  if(v == 0)
    return -1;
  
  if((length == 0) || ((addr + length) > (v->va + v->length)))  // 保证length的合法性
    return -1;
  
  if((addr > v->va) && ((addr + length) < (v->va + v->length)))  // Trying to "dig a hole" inside the memory-mapped region.
    return -1;

  // 如果有回盘需求，则先将映射区中待释放的数据写回磁盘文件中
  if(v->flags & MAP_SHARED)
  {
    munmap_writeback(p->pagetable, addr, length, v);
  }

  // 释放内存页
  uint64 va;  // va must be page-aligned.
  uint64 npages;
  if(addr > v->va)  // 头部有所保留 -> 保留部分所在的pages不能释放
  {
    va = PGROUNDUP(addr);
    npages = (PGROUNDUP(addr + length) - va) / PGSIZE;
  }
  else  // 头部没有保留 -> addr == v->va
  {
    va = PGROUNDDOWN(addr);
    if((addr + length) < (v->va + v->length))  // 尾部有保留
    {
      npages = (PGROUNDDOWN(addr + length) - va) / PGSIZE;
    }
    else  // 尾部没有保留 -> (addr + length) == (v->va + v->length)
    {
      npages = (PGROUNDUP(addr + length) - va) / PGSIZE;
    }
  }
  uvmunmap(p->pagetable, va, npages, 1);

  // 更新vma
  v->length -= length;
  if(v->length <= 0)  // 该映射区已全部释放完
  {
    fileclose(v->f);
    v->valid = 0;
  }
  else
  {
    uint64 temp = v->va;
    v->va = addr > v->va ? v->va : v->va + length;
    v->offset += v->va - temp;
  }

  return 0;
}

uint64
sys_munmap(void)
{
  struct proc *p = myproc();
  
  uint64 addr;
  uint length;

  if(argaddr(0, &addr) < 0 || argint(1, (int*)&length) < 0)
    return -1;

  return munmap(p, addr, length);
}
