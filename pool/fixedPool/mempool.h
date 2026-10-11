#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h>

/* 定长内存池（C 语言版）：块大小固定，分配/释放 O(1)，池满自动按页扩容 */

#define MEM_PAGE_SIZE 4096 /* 每次向系统申请的一页大小 */

/* 页头：每页的开头放一个 next 指针，把所有页串成链表，便于销毁 */
typedef struct mempage_s
{
    struct mempage_s *next;
    /* 后面紧跟数据区，切成若干 block */
} mempage_t;

typedef struct mempool_s
{
    size_t block_size;      /* 对齐后的块大小 */
    size_t blocks_per_page; /* 每页能切多少块 */
    size_t free_count;      /* 当前空闲块数 */
    size_t total_count;     /* 池中块的总数（所有页） */
    size_t page_count;      /* 已申请的页数 */
    char *free_ptr;         /* 自由链表头（嵌入指针法） */
    mempage_t *pages;       /* 页链表头 */
} mempool_t;

/* 创建内存池。block_size 任意正值即可，内部会对齐。
 * 成功返回 0；参数非法返回 -1；内存不足返回 -2。 */
int memp_create(mempool_t *m, size_t block_size);

/* 销毁内存池，释放所有页（允许 m 为 NULL） */
void memp_destroy(mempool_t *m);

/* 分配一个块；池满会自动加页。失败（仅系统内存不足时）返回 NULL */
void *memp_alloc(mempool_t *m);

/* 归还一个块。返回 0 成功；-1 参数为空；
 * 编译时加 -DMEMPOOL_DEBUG 可检测重复释放，命中返回 -2 */
int memp_free(mempool_t *m, void *ptr);

/* 当前已分配（正在使用）的块数 */
size_t memp_used(const mempool_t *m);

/* 一键回收：所有块回到空闲态，但页不还给操作系统（下次直接用） */
void memp_reset(mempool_t *m);

#endif /* MEMPOOL_H */