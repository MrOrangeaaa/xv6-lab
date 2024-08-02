// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// #define NBUCKET 13
// #define HASH(dev, blockno) ((((dev)<<27)|(blockno)) % NBUCKET)
#define NBUCKET 31
#define HASH(dev, blockno) ((((dev) * 131) + (blockno) * 137) % NBUCKET)


/*--- old version ---*/
// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   // 维护了一个双向循环链表 -> LRU cache list
//   struct buf head;
// } bcache;  //buffer cache -> 创建了一个全局的cache，里面存放了很多buf

/*--- new version ---*/
struct {
  struct buf buf[NBUF];  // 所有<磁盘块>缓存(buf)都存储在这个数组里
  struct buf bucket[NBUCKET];  // 创建一个散列表 -> 本质上是创建一组虚拟头结点
  struct spinlock bucket_lock[NBUCKET];  // 给散列表的每一个bucket都配了一把锁
  struct spinlock eviction_lock;  // 驱逐锁
} bcache;


/*--- old version ---*/
// void
// binit(void)
// {
//   struct buf *b;

//   initlock(&bcache.lock, "bcache");

//   // Create linked list of buffers
//   bcache.head.prev = &bcache.head;
//   bcache.head.next = &bcache.head;
//   for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     initsleeplock(&b->lock, "buffer");
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
// }

/*--- new version ---*/
const uint8 bcache_lk_name_sz = sizeof("bcache_bucket_xxx");
char bcache_lk_name[NBUCKET][sizeof("bcache_bucket_xxx")];

void
binit(void)
{
  //初始化散列表
  for(int i = 0; i < NBUCKET; i++)
  {
    bcache.bucket[i].next = 0;

    snprintf(bcache_lk_name[i], bcache_lk_name_sz, "bcache_bucket_%d", i);
    initlock(&bcache.bucket_lock[i], bcache_lk_name[i]);
  }
  
  //初始化所有buf
  for(int i = 0; i < NBUF; i++)
  {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");

    //先将所有buf都放进bucket[0]
    b->next = bcache.bucket[0].next;
    bcache.bucket[0].next = b;
  }

  initlock(&bcache.eviction_lock, "bcache_eviction");
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.

/*--- old version ---*/
// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b;

//   acquire(&bcache.lock);

//   // Is the block already cached?
//   for(b = bcache.head.next; b != &bcache.head; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }

//   // Not cached.
//   // Recycle the least recently used (LRU) unused buffer.
//   for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
//     if(b->refcnt == 0) {
//       b->dev = dev;
//       b->blockno = blockno;
//       b->valid = 0;
//       b->refcnt = 1;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   panic("bget: no buffers");
// }

/*--- new version ---*/
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 计算blockno对应的哈希值，以确定target bucket
  uint key = HASH(dev, blockno);

again:
  acquire(&bcache.bucket_lock[key]);

  // Is the block already cached?
  for(b = bcache.bucket[key].next; b != 0; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.bucket_lock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  release(&bcache.bucket_lock[key]);  //先把手里的bucket让出来，别的线程兴许还等着用呢
  acquire(&bcache.eviction_lock);  //获取驱逐锁 -> 驱逐锁只有一把，也就意味着<块缓存驱逐>是不允许并行的

  // Check again.
  for(b = bcache.bucket[key].next; b != 0; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      release(&bcache.eviction_lock);
      goto again;
    }
  }

  // Still not cached.
  // Evict <least-recently-used buf>.
  struct buf *least_buf = 0;  // <least-recently-used buf>
  struct buf *prev_node_of_least_buf = 0;  // the previous node of the <least-recently-used buf> node in its bucket
  uint bucket_idx_of_least_buf = -1;  // index of the bucket where the <least-recently-used buf> appears
  for(int i = 0; i < NBUCKET; i++)
  {
    int flag_bucket_bingo = 0;  // 1: a new <least-recently-used buf> was found in the current bucket.

    acquire(&bcache.bucket_lock[i]);
    
    for(b = &bcache.bucket[i]; b->next != 0; b = b->next)
    {
      // 更新least_buf & prev_node_of_least_buf
      if(b->next->refcnt == 0 && (!least_buf || b->next->lastused < least_buf->lastused))
      {
        prev_node_of_least_buf = b;
        least_buf = b->next;
        flag_bucket_bingo = 1;
      }
    }

    if(!flag_bucket_bingo)
    {
      release(&bcache.bucket_lock[i]);
    }
    else
    {
      if(bucket_idx_of_least_buf != -1)
        release(&bcache.bucket_lock[bucket_idx_of_least_buf]);
      bucket_idx_of_least_buf = i;
    }
  }

  // 找不到可驱逐的buf
  if(!least_buf)
    panic("bget: no buffers");
  
  // 如果<待驱逐的buf(least-recently-used buf)>不在target bucket中
  if(bucket_idx_of_least_buf != key)
  {
    // Remove the <least-recently-used buf> from its original bucket.
    prev_node_of_least_buf->next = least_buf->next;
    release(&bcache.bucket_lock[bucket_idx_of_least_buf]);
    // Add the <least-recently-used buf> to the target bucket.（头插）
    acquire(&bcache.bucket_lock[key]);
    least_buf->next = bcache.bucket[key].next;
    bcache.bucket[key].next = least_buf;
  }
  
  least_buf->dev = dev;
  least_buf->blockno = blockno;
  least_buf->valid = 0;
  least_buf->refcnt = 1;
  
  release(&bcache.bucket_lock[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&least_buf->lock);
  return least_buf;
}



// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}


// Release a locked buffer.
// Move to the head of the most-recently-used list.

/*--- old version ---*/
// void
// brelse(struct buf *b)
// {
//   if(!holdingsleep(&b->lock))
//     panic("brelse");

//   releasesleep(&b->lock);

//   acquire(&bcache.lock);
//   b->refcnt--;
//   if (b->refcnt == 0) {
//     // no one is waiting for it.
//     b->next->prev = b->prev;
//     b->prev->next = b->next;
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
  
//   release(&bcache.lock);
// }

/*--- new version ---*/
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = HASH(b->dev, b->blockno);

  acquire(&bcache.bucket_lock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    /**
     * 这里关于LRU cache的实现很有意思
     * 我们只需要在buf被彻底闲置的时候更新一次last-used-time即可
     */
    b->lastused = ticks;
  }
  release(&bcache.bucket_lock[key]);
}


/*--- old version ---*/
// void
// bpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt++;
//   release(&bcache.lock);
// }

/*--- new version ---*/
void
bpin(struct buf *b) {
  uint key = HASH(b->dev, b->blockno);

  acquire(&bcache.bucket_lock[key]);
  b->refcnt++;
  release(&bcache.bucket_lock[key]);
}


/*--- old version ---*/
// void
// bunpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt--;
//   release(&bcache.lock);
// }

/*--- new version ---*/
void
bunpin(struct buf *b) {
  uint key = HASH(b->dev, b->blockno);

  acquire(&bcache.bucket_lock[key]);
  b->refcnt--;
  release(&bcache.bucket_lock[key]);
}
