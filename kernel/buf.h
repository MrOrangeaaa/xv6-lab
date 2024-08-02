//<磁盘块>缓存 -> 每个struct buf对象就是一个“块缓存”

/*--- old version ---*/
// struct buf {
//   int valid;   // has data been read from disk?
//   int disk;    // does disk "own" buf?
//   uint dev;
//   uint blockno;
//   struct sleeplock lock;
//   uint refcnt;
//   struct buf *prev; // LRU cache list
//   struct buf *next;
//   uchar data[BSIZE];
// };

/*--- new version ---*/
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint lastused;  // last-used-time
  struct buf *next;
  uchar data[BSIZE];
};
