#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <dirent.h>

/* 参数解析 */
/* --------------------------------------------------------------------------------------------- */
#define true ((unsigned char)(1))
#define false ((unsigned char)(0))

/* 打印信息 */
static const char *help_message = "Usage: pstree [-pn]\n   or: pstree -V\n"
                                  "Display a tree of processes.\n"
                                  "  -p, --show-pids     show PIDs; implies -c\n"
                                  "  -n, --numeric-sort  sort output by PID\n"
                                  "  -V, --version       display version information\n";

/* 命令行参数结构 */
static struct option long_options[] = {
    {"show-pids", no_argument, NULL, 'p'},
    {"numeric-sort", no_argument, NULL, 'n'},
    {"version", no_argument, NULL, 'V'},
    {0, 0, 0, 0}};

/* 记录参数 */
static unsigned char pFlag = false;
static unsigned char nFlag = false;

/* 命令行参数解析 */
void parse_args(int argc, char *argv[])
{
  char ch;
  int option_index = 0;

  while (-1 != (ch = getopt_long(argc, argv, "pnV", long_options, &option_index)))
  {
    switch (ch)
    {
    case 'p':
      pFlag = true;
      break;
    case 'n':
      nFlag = true;
      break;
    case 'V':
      fprintf(stderr, "pstree version: 2023\n");
      exit(0);
      break;
    default:
      fprintf(stdin, "%s", help_message);
      exit(1);
    }
  }
}
/* --------------------------------------------------------------------------------------------- */

/* 动态扩容数组 */
/* --------------------------------------------------------------------------------------------- */
typedef struct VECTOR
{
  void *vec;
  long int capacity, size;
} Vector;

static Vector *__VECTOR_INIT(unsigned int type_size, unsigned int vector_capacity)
{
  Vector *vector = (Vector *)malloc(sizeof(Vector));
  if (vector == NULL)
  {
    printf("Memory allocation failed");
    exit(1);
  }

  vector->vec = malloc(type_size * vector_capacity);
  if (vector->vec == NULL)
  {
    printf("Memory allocation failed");
    exit(1);
  }

  vector->size = 0;
  vector->capacity = vector_capacity;
  return vector;
}

static void __VECTOR_PUSH(Vector *vector, unsigned int type_size, void *element)
{
  if (vector->size == vector->capacity)
  {
    vector->vec = realloc(vector->vec, type_size * vector->capacity * 2);
    if (vector->vec == NULL)
    {
      printf("Memory allocation failed");
      exit(1);
    }
    vector->capacity *= 2;
  }

  unsigned char *src = (unsigned char *)element;
  unsigned char *dst = ((unsigned char *)(vector->vec)) + type_size * (vector->size++);
  for (int i = 0; i < type_size; ++i)
  {
    dst[i] = src[i];
  }
}

#define Vector_Init(type, capacity) (__VECTOR_INIT(sizeof(type), (capacity)))
#define Vector_Push(type, vector, element) (__VECTOR_PUSH(vector, sizeof(type), (element)))
#define Vector_Get(type, vector, idx) ((type)(((type *)(vector->vec))[(idx)]))
/* --------------------------------------------------------------------------------------------- */

/* 将所有pid放入一个vector中*/
/* --------------------------------------------------------------------------------------------- */

/* 将文件名转换为数字 */
static int dirent_to_pid(struct dirent *dirItem)
{
  pid_t pid = 0;

  if (dirItem->d_type == DT_DIR)
  {
    char *name = dirItem->d_name;
    for (int i = 0; name[i]; ++i)
    {
      if (name[i] > '9' || name[i] < '0')
      {
        return -1;
      }
      pid = pid * 10 + name[i] - '0';
    }
    return pid;
  }
  return -1;
}

/* 写入pid */
static void push_pid(Vector *pids)
{
  DIR *dir = opendir("/proc/");
  if (dir == NULL)
  {
    printf("Fail to open /proc");
    exit(1);
  }

  struct dirent *dirItem = NULL;
  while ((dirItem = readdir(dir)) != NULL)
  {
    pid_t pid = dirent_to_pid(dirItem);
    if (pid > 0)
    {
      Vector_Push(pid_t, pids, &pid);
    }
  }
}
/* --------------------------------------------------------------------------------------------- */

/* 建树 */
/* --------------------------------------------------------------------------------------------- */
typedef struct NODE
{
  pid_t pid;
  char *comm;
  Vector *children_ids;
} Node;

static Vector *nodes = NULL;

/* 由于C没有哈希，处于跨平台要求，只好使用2分搜索查找父节点的索引 */
static int get_parent(pid_t ppid)
{
  if (nodes == NULL)
  {
    return 0;
  }

  int left = 0, right = nodes->size;
  while (left < right)
  {
    int mid = left + (right - left) / 2;
    pid_t mid_pid = Vector_Get(Node *, nodes, mid)->pid;
    if (mid_pid == ppid)
    {
      return mid;
    }
    else if (mid_pid > ppid)
    {
      right = mid;
    }
    else
    {
      left = mid + 1;
    }
  }
  return -1;
}

/* 构建树 */
static void make_node(pid_t pid)
{
  char pstat[24] = {0}, comm[17] = {0};
  pid_t ppid = 0;
  int ppidx = -1;
  sprintf(pstat, "/proc/%d/stat", (int)pid);

  FILE *fstat = fopen(pstat, "r");
  if (!fstat)
  {
    return;
  }

  fscanf(fstat, "%d (%s %c %d", (int *)pstat, comm, pstat, &ppid);
  if ((ppidx = get_parent(ppid)) >= 0)
  {
    Node *node = (Node *)malloc(sizeof(Node));
    node->pid = pid;
    node->children_ids = NULL;

    int len = strlen(comm);
    node->comm = (char *)malloc(len);
    strcpy(node->comm, comm);
    node->comm[len - 1] = 0;

    if (!nodes)
    {
      nodes = Vector_Init(Node *, 1);
      Vector_Push(Node *, nodes, &node);
    }
    else
    {
      Node *parent = Vector_Get(Node *, nodes, ppidx);
      if (!parent->children_ids)
      {
        parent->children_ids = Vector_Init(int, 1);
      }
      Vector_Push(int, parent->children_ids, &(nodes->size));
      Vector_Push(Node *, nodes, &node);
    }
  }
  fclose(fstat);
}

/* --------------------------------------------------------------------------------------------- */

/* DFS打印树 */
/* --------------------------------------------------------------------------------------------- */
static void dfs_print(Node *node, char *prefix, char *symb)
{
  /* 打印连接符 */
  if (symb)
  {
    printf("%s", symb);
  }

  /* 根据参数决定输出的进程名称形式，并打印 */
  char *pname;
  if (pFlag)
  {
    if (asprintf(&pname, "%s(%d)", node->comm, node->pid) < 0)
    {
      printf("fail to print process name and pid.");
    };
  }
  else
  {
    if (asprintf(&pname, "%s", node->comm) < 0)
    {
      printf("fail to print process name.");
    };
  }
  printf("%s", pname);

  /* 如果是叶子节点打印后直接返回 */
  if (!node->children_ids)
  {
    printf("\n");
    return;
  }

  /* 根据新的进程名称，扩展每行要输出的前缀空格数量 */
  char *newprefix;
  if (asprintf(&newprefix, "%s", prefix) < 0)
  {
    printf("fail to generate newprefix.");
  }
  for (int i = 0; pname[i]; ++i)
  {
    asprintf(&newprefix, "%s%s", newprefix, " ");
  }
  free(pname);

  /* 递归打印 */
  for (int i = 0; i < node->children_ids->size; ++i)
  {
    /* 按顺序拿到子节点 */
    int child_id = Vector_Get(int, node->children_ids, i);
    Node *child = Vector_Get(Node *, nodes, child_id);

    /* 根据子节点的位置给前缀添加竖线 */
    char *nextprefix;
    asprintf(&nextprefix, "%s", newprefix);

    if (i == 0)
    {
      if (node->children_ids->size == 1)
      {
        asprintf(&nextprefix, "%s%s", nextprefix, "   ");
        dfs_print(child, nextprefix, "───");
        free(nextprefix);
      }
      else
      {
        asprintf(&nextprefix, "%s%s", nextprefix, " | ");
        dfs_print(child, nextprefix, "─┬─");
        free(nextprefix);
      }
    }
    else if (i == node->children_ids->size - 1)
    {
      printf("%s", nextprefix);
      asprintf(&nextprefix, "%s%s", nextprefix, "   ");
      dfs_print(child, nextprefix, " └─");
      free(nextprefix);
    }
    else
    {
      printf("%s", nextprefix);
      asprintf(&nextprefix, "%s%s", nextprefix, " | ");
      dfs_print(child, nextprefix, " ├─");
      free(nextprefix);
    }
  }
  free(newprefix);
}
/* --------------------------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
  /* 解析参数 */
  parse_args(argc, argv);

  /* 读取所有 pid 并放入一个 Vector*/
  Vector *pids = Vector_Init(pid_t, 8);
  push_pid(pids);

  if (nFlag)
  {
    /* 排序 pids(其实默认顺序已排好，所以此处看不出区别) */
    int comp(const void *ptr1, const void *ptr2)
    {
      return *((pid_t *)ptr1) > *((pid_t *)ptr2);
    }

    qsort(pids->vec, pids->size, sizeof(pid_t), comp);
  }

  /* 建树 */
  for (int i = 0; i < pids->size; ++i)
  {
    pid_t pid = Vector_Get(pid_t, pids, i);
    make_node(pid);
  }

  /* 打印树 */
  Node *root = Vector_Get(Node *, nodes, 0);
  dfs_print(root, "", NULL);

  return 0;
}

/* 效果 */
/*
init(1)─┬─init(11)───init(12)─┬─sh(13)───sh(14)───sh(19)───node(23)─┬─node(34)─┬─zsh(145)
        |                     |                                     |          ├─zsh(920)───make(25714)───pstree-64(25720)
        |                     |                                     |          └─zsh(15848)
        |                     |                                     ├─node(68)
        |                     |                                     └─node(103)─┬─cpptools(174)
        |                     |                                                 └─node(3252)
        |                     └─cpptools-srv(1511)
        ├─init(3105)───init(3106)───node(3107)
        └─init(3114)───init(3115)───node(3116)
*/