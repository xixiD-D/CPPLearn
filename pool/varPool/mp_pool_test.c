/* 编译：gcc -O2 -Wall -o mp_pool_test mp_pool.c mp_pool_test.c */
#include "mp_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond)                                         \
    do                                                      \
    {                                                       \
        if (!(cond))                                        \
        {                                                   \
            printf("FAIL  (行 %d): %s\n", __LINE__, #cond); \
            return 0;                                       \
        }                                                   \
    } while (0)

/* 测试 1：小块基本分配 / 读写 */
static int test_basic(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    unsigned char *p[10];
    int i, j;
    for (i = 0; i < 10; i++)
    {
        p[i] = mp_alloc(pool, 512);
        CHECK(p[i] != NULL);
        memset(p[i], i & 0xFF, 512);
    }
    for (i = 0; i < 10; i++) /* 内容互不干扰 */
        for (j = 0; j < 512; j++)
            CHECK(p[i][j] == (unsigned char)(i & 0xFF));

    CHECK(mp_pool_used(pool) >= 5120);
    mp_destroy_pool(pool);
    return 1;
}

/* 测试 2：mp_alloc 返回地址 32 字节对齐 */
static int test_alignment(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    int i;
    for (i = 1; i <= 64; i++)
    {
        void *p = mp_alloc(pool, (size_t)i); /* 各种尺寸打乱对齐 */
        CHECK(p != NULL);
        CHECK((uintptr_t)p % MP_ALIGNMENT == 0);
    }
    mp_destroy_pool(pool);
    return 1;
}

/* 测试 3：mp_calloc 确实清零 */
static int test_calloc(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    char *p = mp_calloc(pool, 256);
    CHECK(p != NULL);
    int i;
    for (i = 0; i < 256; i++)
        CHECK(p[i] == 0);

    /* 写脏后 reset，再 calloc 依然为零（验证 reset 归位 + calloc 清零） */
    memset(p, 0xAB, 256);
    mp_reset_pool(pool);
    p = mp_calloc(pool, 256);
    for (i = 0; i < 256; i++)
        CHECK(p[i] == 0);

    mp_destroy_pool(pool);
    return 1;
}

/* 测试 4：大块分配 / 单独释放 / 空槽复用 */
static int test_large(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    void *a = mp_alloc(pool, 8192); /* > 4095 -> 大块 */
    CHECK(a != NULL);
    memset(a, 0x5A, 8192);
    CHECK(mp_free(pool, a) == 0); /* 大块可单独释放 */

    void *b = mp_alloc(pool, 8192); /* 应复用空槽，链不再变长 */
    CHECK(b != NULL);
    CHECK(pool->large != NULL && pool->large->next == NULL);

    void *small = mp_alloc(pool, 128);
    CHECK(mp_free(pool, small) == 1); /* 小块：设计不单独释放 */
    CHECK(mp_free(pool, NULL) == -1); /* 参数检查 */

    mp_destroy_pool(pool);
    return 1;
}

/* 测试 5：跨页扩展 + 数据完整（58×256 必然触发 mp_alloc_block） */
static int test_grow(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    unsigned char *p[58];
    int i, j;
    for (i = 0; i < 58; i++)
    {
        p[i] = mp_alloc(pool, 256);
        CHECK(p[i] != NULL);
        memset(p[i], i & 0xFF, 256);
    }
    CHECK(pool->head.next != NULL); /* 确实扩了新页 */
    for (i = 0; i < 58; i++)
        for (j = 0; j < 256; j++)
            CHECK(p[i][j] == (unsigned char)(i & 0xFF));

    mp_destroy_pool(pool);
    return 1;
}

/* 测试 6：mp_reset_pool 后统计归零、可继续正常使用 */
static int test_reset(void)
{
    mp_pool_t *pool = mp_create_pool(4096);
    CHECK(pool != NULL);

    int i;
    for (i = 0; i < 58; i++)
        CHECK(mp_alloc(pool, 256) != NULL);
    CHECK(mp_alloc(pool, 8192) != NULL); /* 再压一个大块 */
    CHECK(mp_pool_used(pool) > 0);

    mp_reset_pool(pool);
    CHECK(mp_pool_used(pool) == 0);
    CHECK(pool->large == NULL);
    CHECK(pool->current == &pool->head);
    CHECK(pool->head.failed == 0);

    for (i = 0; i < 58; i++)
    { /* reset 后照常工作 */
        unsigned char *p = mp_alloc(pool, 256);
        CHECK(p != NULL);
        p[0] = 0x77;
        CHECK(p[0] == 0x77);
    }
    mp_destroy_pool(pool);
    return 1;
}

/* 性能对比：每轮 1 万个随机小块，共 100 轮。
 * 池：每轮结束 reset；malloc：逐个 free */
static void bench(void)
{
    const int ROUNDS = 100, BATCH = 10000;
    volatile long sink = 0;
    void *ptrs[BATCH];

    { /* malloc/free 基线 */
        srand(42);
        clock_t t0 = clock();
        int r, i;
        for (r = 0; r < ROUNDS; r++)
        {
            for (i = 0; i < BATCH; i++)
            {
                size_t sz = rand() % 240 + 16;
                ptrs[i] = malloc(sz);
                sink += (long)(uintptr_t)ptrs[i] % 7;
            }
            for (i = 0; i < BATCH; i++)
                free(ptrs[i]);
        }
        printf("  malloc/free : %.1f ms\n", (double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
    }
    { /* 内存池 + 每轮 reset */
        mp_pool_t *pool = mp_create_pool(4096);
        srand(42);
        clock_t t0 = clock();
        int r, i;
        for (r = 0; r < ROUNDS; r++)
        {
            for (i = 0; i < BATCH; i++)
            {
                size_t sz = rand() % 240 + 16;
                ptrs[i] = mp_alloc(pool, sz);
                sink += (long)(uintptr_t)ptrs[i] % 7;
            }
            mp_reset_pool(pool);
        }
        printf("  mp_pool     : %.1f ms\n", (double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
        mp_destroy_pool(pool);
    }
    (void)sink;
}

int main(void)
{
    printf("========== nginx 风格内存池验证 ==========\n");
    printf("[测试1] 小块基本分配/读写 : %s\n", test_basic() ? "PASS" : "FAIL");
    printf("[测试2] 32 字节对齐       : %s\n", test_alignment() ? "PASS" : "FAIL");
    printf("[测试3] calloc 清零       : %s\n", test_calloc() ? "PASS" : "FAIL");
    printf("[测试4] 大块分配/释放     : %s\n", test_large() ? "PASS" : "FAIL");
    printf("[测试5] 跨页扩展          : %s\n", test_grow() ? "PASS" : "FAIL");
    printf("[测试6] reset 整池回收    : %s\n", test_reset() ? "PASS" : "FAIL");
    printf("[性能] 每轮 1 万随机小块 x 100 轮:\n");
    bench();
    return 0;
}