struct stat;
struct rtcdate;
struct sysinfo;

// 汇编语言实现
// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
#ifdef LAB_PGTBL
int pgaccess(void*, uint8, uint64*);
#endif
#ifdef LAB_NET
int connect(uint32, uint16, uint16);
#endif


// C语言实现
// user/ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
#ifdef LAB_PGTBL
int ugetpid(void);
#endif
// user/umalloc.c
void* malloc(uint);
void free(void*);
// user/printf.c
void fprintf(int, const char*, ...);
void printf(const char*, ...);
// 暂时未定义
int statistics(void*, int);

