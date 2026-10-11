#include "mp_pool.h"

#include <stdlib.h>
#include <string.h>

/**
 * 分配的起始地址都要跳过节点头，head 节点内嵌在池头后面，同样适用
 */
static unsigned char *node_data_start(mp_node_t *node)
{
    return (unsigned char *)node + sizeof(mp_node_t);
}

mp_pool_t *mp_create_pool(size_t size)
{
    mp_pool_t *p;
    if (size == 0)
    {
        return nullptr;
    }

    int ret = posix_memalign((void **)&p, MP_ALIGNMENT, size + sizeof(mp_pool_t));

    if (ret)
    {
        return nullptr;
    }

    p->max = (size < MP_MAX_ALLOC_FROM_POOL) ? size : MP_MAX_ALLOC_FROM_POOL;
    p->current = &p->head;
    p->large = nullptr;

    p->head.last = (unsigned char *)p + sizeof(mp_pool_t);
    p->head.end = p->head.last + size;
    p->head.next = nullptr;

    p->head.failed = 0;
    return p;
}

void mp_destroy_pool(mp_pool_t *pool)
{
    if (!pool)
    {
        return;
    }

    mp_large_t *l;

    for (l = pool->large; l; l = l->next)
    {
        free(l->alloc);
    }

    mp_node_t *h = pool->head.next;
    while (h)
    {
        mp_node_t *next = h->next;
        free(h);
        h = next;
    }

    free(pool);
}

void mp_reset_pool(mp_pool_t *pool)
{
    if (!pool)
    {
        return;
    }

    mp_large_t *l;

    for (l = pool->large; l; l = l->next)
    {
        free(l->alloc);
        pool->large = nullptr;
    }

    mp_node_t *h;
    for (h = &pool->head; h; h = h->next)
    {
        h->last = node_data_start(h);
        h->failed = 0;
    }
    pool->current = &pool->head;
}

/**
 * 所有页都放不下时，向系统申请新的一页
 */
static void *mp_alloc_block(mp_pool_t *pool, size_t size)
{
    // 新页大小与首页保持一致，节点头 + 数据区
    size_t psize = (size_t)(pool->head.end - (unsigned char *)&pool->head);

    unsigned char *m;
    int ret = posix_memalign((void **)&m, MP_ALIGNMENT, psize);
    if (ret)
    {
        return nullptr;
    }

    mp_node_t *new_node = (mp_node_t *)m;
    new_node->end = m + psize;
    new_node->next = nullptr;
    new_node->failed = 0;

    m += sizeof(mp_node_t);
    m = mp_align_ptr(m, MP_ALIGNMENT);
    new_node->last = m + size;

    mp_node_t *p, *current = pool->current;

    for (p = current; p->next; p = p->next)
    {
        if (p->failed++ > 4)
        {
            current = p->next;
        }
    }
    p->next = new_node;
    pool->current = current ? current : new_node;
    return m;
}

/**
 * 大块：直接 malloc，挂到 large 链表（优先复用已释放的空槽）
 */
static void *mp_alloc_large(mp_pool_t *pool, size_t size)
{
    void *p = malloc(size);
    if (!p)
    {
        return nullptr;
    }

    size_t n = 0;
    mp_large_t *large;
    for (large = pool->large; large; large = large->next)
    {
        if (large->alloc == nullptr)
        {
            large->alloc = p;
            return p;
        }
        if (n++ > 3)
        {
            break;
        }
    }

    large = mp_alloc(pool, sizeof(mp_large_t));
    if (!large)
    {
        free(p);
        return nullptr;
    }
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;
    return p;
}

void *mp_memalignn(mp_pool_t *pool, size_t size, size_t alignment)
{
    void *p;
    if (!pool)
    {
        return nullptr;
    }
    int ret = posix_memalign(&p, alignment, size);
    if (ret)
    {
        return nullptr;
    }

    mp_large_t *large = mp_alloc(pool, sizeof(mp_large_t));

    if (!large)
    {
        free(p);
        return nullptr;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;
    return p;
}

void *mp_alloc(mp_pool_t *pool, size_t size)
{
    if (!pool || size <= 0)
    {
        return nullptr;
    }

    if (size <= pool->max)
    {
        mp_node_t *p = pool->current;
        do
        {
            unsigned char *m = mp_align_ptr(p->last, MP_ALIGNMENT);
            if ((size_t)(p->end - m) >= size)
            {
                p->last = m + size;
                return m;
            }
            p = p->next;
        } while (p);
        return mp_alloc_block(pool, size);
    }
    return mp_alloc_large(pool, size);
}

void *mp_nalloc(mp_pool_t *pool, size_t size)
{
    if (!pool || size == 0)
        return NULL;

    if (size <= pool->max)
    {
        mp_node_t *p = pool->current;
        do
        {
            unsigned char *m = p->last; /* 不做对齐，省内存 */
            if ((size_t)(p->end - m) >= size)
            {
                p->last = m + size;
                return m;
            }
            p = p->next;
        } while (p);
        return mp_alloc_block(pool, size);
    }
    return mp_alloc_large(pool, size);
}

void *mp_calloc(mp_pool_t *pool, size_t size)
{
    void *p = mp_alloc(pool, size);
    if (p)
    {
        memset(p, 0, size);
    }
    return p;
}

int mp_free(mp_pool_t *pool, void *p)
{
    if (!pool || !p)
    {
        return -1;
    }

    mp_large_t *l;

    for (l = pool->large; l; l = l->next)
    {
        if (p == l->alloc)
        {
            free(l->alloc);
            l->alloc = nullptr;
            return 0;
        }
    }
    return 1;
}

size_t mp_pool_used(const mp_pool_t *pool) {
    if (!pool) {
        return 0;
    }

    size_t used = 0;
    const mp_node_t *h;
    for (h = &pool->head; h; h = h->next) {
        used += (size_t)(h->last - node_data_start((mp_node_t *)h));
    }

    return used;
}