struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref;            // reference count
  char readable;
  char writable;
  struct pipe *pipe;  // FD_PIPE
  struct inode *ip;   // FD_INODE and FD_DEVICE
  uint off;           // FD_INODE
  short major;        // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// In-memory copy of an inode -> 将dinode中的内容从磁盘拷贝到内存中，并且添加了一些辅助字段
struct inode {
  uint dev;                 // Device number
  uint inum;                // Inode number
  int ref;                  // Reference count
  struct sleeplock lock;    // Protects everything below here
  int valid;                // Inode has been read from disk?

  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 2];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
