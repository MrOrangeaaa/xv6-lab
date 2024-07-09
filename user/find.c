#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find_helper(const char *path, const char *target)
{
	char buf[512];
	int fd;
	struct stat st;

	if((fd = open(path, 0)) < 0){
		fprintf(2, "find: cannot open %s\n", path);
		exit(1);
	}

	if(fstat(fd, &st) < 0){  //获取文件的状态
		fprintf(2, "find: cannot stat %s\n", path);
		exit(1);
	}

	switch(st.type)
    {
		case T_FILE:  //普通文件
			fprintf(2, "Usage: find dir file\n");
			exit(1);
		case T_DIR:  //目录

            //检查路径是否过长 -> 注意要为即将拼接进来的 "/ + de.name + \0" 预留空间
			if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf))
            {
				printf("find: path too long\n");
				break;
			}

            //路径末尾加个'/'
            char *p;
			strcpy(buf, path);
			p = buf + strlen(buf);
			*(p++) = '/';

            /**
             * Directory is a file containing a sequence of dirent structures.
             * 故只需逐个读取目录文件中的每一项
             */
            struct dirent de;  //目录项
			while(read(fd, &de, sizeof(de)) == sizeof(de))
            {
                //跳过 inode为0(不可用) 和 "."/".."
				if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
					continue;
                
                //实现strcat()的功能，在末尾拼接字符串
				memmove(p, de.name, DIRSIZ);
				p[DIRSIZ] = '\0';

                //检查目标文件的状态
				if(stat(buf, &st) < 0){
        			printf("find: cannot stat %s\n", buf);
        			continue;
      			}
      			if(st.type == T_DIR){
      				find_helper(buf, target);
      			}else if (st.type == T_FILE){
      				if (strcmp(de.name, target) == 0)
      				{
      					printf("%s\n", buf);
      				}
      			}
			}
			break;
	}

	close(fd);
}

int main(int argc, char const *argv[])
{
	if (argc != 3)
	{
		fprintf(2, "Usage: find dir file\n");
		exit(1);
	}

	const char *path = argv[1];
	const char *target = argv[2];
	find_helper(path, target);
	exit(0);
}