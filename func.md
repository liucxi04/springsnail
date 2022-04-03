## 补充知识

```c++
// __FILE__ : 用以指示本行语句所在源文件的文件名
// __LINE__ : 用以指示本行语句在源文件中的位置信息
```

```c++
int getopt(int argc, char * const argv[], const char *optstring);
//功能：用来分析命令行参数
//参数：argc、argv：由 main 函数直接传入
//	   optstring：一个包含正确的参数选项字符串，用于参数的解析。例如 “abc:”，其中 -a，-b 就表示两个普通选项，-c 表示一个必须有参数的选项，因为它后面有一个冒号

/*
外部变量说明：
	optarg：如果某个选项有参数，这包含当前选项的参数字符串
	optind：argv 的当前索引值
	opterr：正常运行状态下为 0。非零时表示存在无效选项或者缺少选项参数，并输出其错误信息
	optopt：当发现无效选项字符时，即 getopt() 方法返回 ? 字符，optopt 中包含的就是发现的无效选项字符
*/
```

```c++
char *basename(char *path);
//功能：获取文件名
//参数：参数path是文件的路径，这里的文件路径就是/home/linux/txt.c 这种类型而已，basename函数并不会关心路径是否正确，文件是否存在，只不过是把路径上除了最后的txt.c 这个文件名字其他的东西都删除了然后返回
```

```c++
int fstat (int filedes, struct stat *buf);
//功能：由文件描述符取得文件的状态。fstat() 用来将参数 filedes 所指向的文件状态复制到参数 buf 所指向的结构中(struct stat)。
//返回值：执行成功返回0，失败返回-1，错误代码保存在errno中。

//项目中用到了 buf.st_size 来获取文件内容的大小
```

```c++
char *strpbrk(const char *str1, const char *str2) 
//功能：检索字符串 str1 中第一个匹配字符串 str2 中字符的字符，不包含空结束字符。也就是说，依次检验字符串 str1 中的字符，当被检验字符在字符串 str2 中也包含时，则停止检验，并返回该字符位置。
//参数：str1 -- 要被检索的 C 字符串。
//	   str2 -- 该字符串包含了要在 str1 中进行匹配的字符列表。
//返回值：该函数返回 str1 中第一个匹配字符串 str2 中字符的字符数，如果未找到字符则返回 NULL。
```

```c++
char * strstr(const char *haystack, const char *needle)；
//功能：在字符串 haystack 中查找第一次出现字符串 needle 的位置
//参数：haystack：要被检索的 C 字符串。
//	   needle：在 haystack 字符串内要搜索的小字符串。
//返回值：该函数返回在 haystack 中第一次出现 needle 字符串的位置，如果未找到则返回 NULL。
```

