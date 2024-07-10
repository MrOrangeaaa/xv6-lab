#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int readline(int argc, char *argv[])
{
	char buf[1024];
	int n = 0;

	//每次从标准输入读1字节，循环读取
	while(read(0, buf+n, 1) == 1)
	{
		if(n == 1023)  //buf已满
		{
			fprintf(2, "argument is too long\n");
			exit(1);
		}
		if(buf[n] == '\n')  //读到换行符，终止
		{
			break;
		}
		n++;
	}
	buf[n] = '\0';  //共计读取到n个有效字符，保存在buf中

	if(n == 0) return 0;

	//解析buf中的内容，拆分成若干参数，并存入argv[]
	int offset = 0;
	while(offset < n)
	{
		if(argc >= MAXARG - 1)
		{
			fprintf(2, "too many arguments\n");
			exit(1);
		}
		argv[argc++] = buf + offset;

		//将offset移动到当前单词的尾后位置
		while(buf[offset] != ' ' && offset < n)
		{
			offset++;
		}

		//将空格(包括多余空格)替换为空字符，标记当前单词的结尾，并将offset移动到下一个单词的开头
		while(buf[offset] == ' ' && offset < n)
		{
			buf[offset++] = '\0';
		}
	}

	return argc;
}

int main(int argc, const char *argv[])
{
	if (argc < 2)  //至少应该有两个参数
	{
		fprintf(2, "Usage: xargs command (arg1, arg2, ...)\n");
		exit(1);
	}

	char *command = malloc(strlen(argv[1]) + 1);
    strcpy(command, argv[1]);

	char *new_argv[MAXARG];  //MAXARG: 命令行参数个数的上限
	for (int i = 1; i < argc; ++i)
	{
		new_argv[i - 1] = malloc(strlen(argv[i]) + 1);
		strcpy(new_argv[i - 1], argv[i]);
	}

	int new_argc;
	while((new_argc = readline(argc - 1, new_argv)) != 0)
	{
		new_argv[new_argc] = 0;  //在尾后位置添加一个空指针，作为参数列表的结束标记
		if(fork() == 0)
		{
			exec(command, new_argv);
			fprintf(2, "exec[%s] failed\n", command);
			exit(1);
		}else
		{
			wait(0);
			// printf("process[%d] has been safely reclaimed\n", wait(0));
		}
	}

	exit(0);
}