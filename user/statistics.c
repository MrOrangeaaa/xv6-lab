#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
statistics(void *buf, int sz)
{
  int fd, i, n;
  
  fd = open("statistics", O_RDONLY);  //"statistics"这个特殊文件在user/init.c/main()中被创建
  if(fd < 0) {
      fprintf(2, "stats: open failed\n");
      exit(1);
  }
  for (i = 0; i < sz; ) {
    if ((n = read(fd, buf+i, sz-i)) < 0) {
      break;
    }
    i += n;
  }
  close(fd);
  return i;
}
