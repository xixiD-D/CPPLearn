#include "mempool.h"

#include <stdlib.h>
#include <string.h>

/* 块至少要存得下一个指针（自由链表用），并按指针对齐 */
static size_t fix_block_size(size_t size)
{
    size_t align = sizeof(void *);
    if (size < align)
        size = align;
    return (size + align - 1) / align * align;
}

/* 向系统申请新页，并把页内所有块挂到自由链表上 */
static int add_page(mempool_t *m)
{
    mempage_t *page = (mempage_t *)malloc(MEM_PAGE_SIZE);
    if (!page)
        return -1;

    page->next = m->pages; /* 页入链表，供 destroy 统一释放 */
    m->pages = page;
    m->page_count++;

    /* 数据区起点 = 页头之后，切成 blocks_per_page 块串成自由链表 */
    char *ptr = (char *)(page + 1);
    size_t i;
    for (i = 0; i < m->blocks_per_page; i++)
    {
        *(char **)ptr = m->free_ptr; /* 头插法：块 -> 旧链表头 */
        m->free_ptr = ptr;
        ptr += m->block_size;
    }
    m->free_count += m->blocks_per_page;
    m->total_count += m->blocks_per_page;
    return 0;
}

int memp_create(mempool_t *m, size_t block_size)
{
    if (!m || block_size == 0)
        return -1;

    memset(m, 0, sizeof(*m));
    m->block_size = fix_block_size(block_size);
    /* 页头要占一个指针，数据区放不下一块则参数无意义 */
    m->blocks_per_page = (MEM_PAGE_SIZE - sizeof(mempage_t)) / m->block_size;
    if (m->blocks_per_page == 0)
        return -1;

    if (add_page(m) != 0)
        return -2;
    return 0;
}

void memp_destroy(mempool_t *m)
{
    if (!m)
        return;
    while (m->pages)
    {
        mempage_t *next = m->pages->next;
        free(m->pages);
        m->pages = next;
    }
    memset(m, 0, sizeof(*m));
}

void *memp_alloc(mempool_t *m)
{
    if (!m)
        return NULL;
    if (!m->free_ptr && add_page(m) != 0) /* 池满 -> 自动扩容 */
        return NULL;

    void *ptr = m->free_ptr; /* 摘下链表头 */
    m->free_ptr = *(char **)ptr;
    m->free_count--;
    return ptr;
}

int memp_free(mempool_t *m, void *ptr)
{
    if (!m || !ptr)
        return -1;

#ifdef MEMPOOL_DEBUG
    /* 调试模式：遍历自由链表，检测重复释放（O(n)，仅调试用） */
    char *p = m->free_ptr;
    while (p)
    {
        if (p == (char *)ptr)
            return -2;
        p = *(char **)p;
    }
#endif

    *(char **)ptr = m->free_ptr; /* 头插回链表 */
    m->free_ptr = (char *)ptr;
    m->free_count++;
    return 0;
}

size_t memp_used(const mempool_t *m)
{
    if (!m)
        return 0;
    return m->total_count - m->free_count;
}

void memp_reset(mempool_t *m)
{
    if (!m)
        return;

    m->free_ptr = NULL;
    m->free_count = 0;

    /* 把每一页重新切一遍，全部挂回自由链表 */
    mempage_t *page = m->pages;
    while (page)
    {
        char *ptr = (char *)(page + 1);
        size_t i;
        for (i = 0; i < m->blocks_per_page; i++)
        {
            *(char **)ptr = m->free_ptr;
            m->free_ptr = ptr;
            ptr += m->block_size;
        }
        page = page->next;
    }
    m->free_count = m->total_count;
}