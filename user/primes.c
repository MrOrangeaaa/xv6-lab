#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void new_proc(int pipe_left[2])
{
	int prime;  //本级进程对应的素数 -> 从管道里读到的第一个数
	close(pipe_left[1]);  //关闭写端
    int ret = read(pipe_left[0], &prime, 4);
	if(ret == 4)
    {
        printf("prime %d\n", prime);
	}else if(ret == 0)
    {
        fprintf(2, "the last process report: no more nums to filter\n");
		exit(0);
    }else
    {
        fprintf(2, "process[%d] failed to read\n", getpid());
        exit(1);
    }

    int n;
    int pipe_right[2];
    pipe(pipe_right);
    if (fork() == 0)
    {
        new_proc(pipe_right);
    }else
    {
        close(pipe_right[0]);  //关闭读端
        while(read(pipe_left[0], &n, 4) == 4)
        {
            if(n%prime)  //无法整除，筛选通过，发给下一级进程
            {
                write(pipe_right[1], &n, 4);
            }
        }
        close(pipe_left[0]);
        close(pipe_right[1]);
        wait(0);
        // int child_pid = wait(0);
        // printf("process[%d] has been recycled\n", child_pid);
        exit(0);
    }
}

int main(int argc, char const *argv[])
{
	int p[2];
	pipe(p);
	if (fork() == 0)
	{
		new_proc(p);
	}else  //主进程
	{
		close(p[0]);  //关闭读端
		for(int i = 2; i <= 35; i++)
		{
			if (write(p[1], &i, 4) != 4)  //每次写4字节，即一个int
			{
				fprintf(2, "first process failed to write %d into the pipe\n", i);
				exit(1);
			}
		}
		close(p[1]);
        wait(0);
		// int child_pid = wait(0);  //即Linux中的 "wait(NULL);"
        // printf("process[%d] has been recycled\n", child_pid);
		exit(0);
	}
	return 0;
}