#ifndef MP_POOL_H
#define MP_POOL_H

#include <stddef.h>

/* nginx 风格内存池（不定长 · 顺序分配）
 * 生命周期内"只进不退"：小块分配只是推指针，不单独释放；
 * 整批内存通过 mp_reset_pool / mp_destroy_pool 回收。
 * 适合"一批对象同生共死"的场景：HTTP 请求、一次 SQL 解析、一帧计算。 */

#define MP_ALIGNMENT 32 /* 对齐粒度（2 的幂） */
#define MP_PAGE_SIZE 4096
#define MP_MAX_ALLOC_FROM_POOL (MP_PAGE_SIZE - 1) /* 小块上限 */

#define mp_align(n, alignment) (((n) + ((alignment) - 1)) & ~((alignment) - 1))
#define mp_align_ptr(p, alignment) (void *)((((size_t)(p)) + ((alignment) - 1)) & ~((size_t)(alignment) - 1))

/* 大块节点：登记 malloc 出来的大块，销毁池时统一 free */
typedef struct mp_large_s
{
    struct mp_large_s *next;
    void *alloc; /* 大块地址；NULL 表示槽可复用 */
} mp_large_t;

/* 小块页节点：一页 = 节点头 + 数据区，数据区内顺序推指针分配 */
typedef struct mp_node_s
{
    unsigned char *last;    /* 数据区已用到哪里 */
    unsigned char *end;     /* 页尾 */
    struct mp_node_s *next; /* 下一页 */
    size_t failed;          /* 连续分配失败次数（nginx 优化） */
} mp_node_t;

typedef struct mp_pool_s
{
    size_t max;         /* 小块上限 = min(页大小, 4095) */
    mp_node_t *current; /* 分配起始页（跳过失败多的页） */
    mp_large_t *large;  /* 大块链表 */
    mp_node_t head;     /* 首页节点内嵌在池头里 */
} mp_pool_t;

/* 创建池，size 为首页数据区大小（一般传 4096）。失败返回 NULL */
mp_pool_t *mp_create_pool(size_t size);

/* 销毁池：释放所有大块、所有页、池本身（允许 NULL） */
void mp_destroy_pool(mp_pool_t *pool);

/* 整池回收：所有大块 free 掉，所有页回到空态，但页不还给系统 */
void mp_reset_pool(mp_pool_t *pool);

/* 分配 size 字节（32 字节对齐）。小块推指针，大块走 malloc。失败返回 NULL */
void *mp_alloc(mp_pool_t *pool, size_t size);

/* 同 mp_alloc，但不保证对齐（no-align），给字符串等数据用，省内存 */
void *mp_nalloc(mp_pool_t *pool, size_t size);

/* mp_alloc + 清零 */
void *mp_calloc(mp_pool_t *pool, size_t size);

/* 按指定对齐分配（归入大块管理，随池释放） */
void *mp_memalign(mp_pool_t *pool, size_t size, size_t alignment);

/* 释放"大块"。返回 0 = 已释放；1 = p 是小块（设计上不单独释放）；-1 = 参数为空 */
int mp_free(mp_pool_t *pool, void *p);

/* 统计所有页数据区已用字节数（不含页头，含对齐浪费） */
size_t mp_pool_used(const mp_pool_t *pool);

#endif /* MP_POOL_H */