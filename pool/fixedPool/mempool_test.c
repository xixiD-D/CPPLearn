/* 编译：gcc -O2 -Wall -o mempool_test mempool.c mempool_test.c */
#include "mempool.h"

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

/* 测试 1：基本分配 / 读写 / 释放 */
static int test_basic(void)
{
    mempool_t m;
    CHECK(memp_create(&m, 32) == 0);

    void *p[100];
    int i;
    for (i = 0; i < 100; i++)
    {
        p[i] = memp_alloc(&m);
        CHECK(p[i] != NULL);
        memset(p[i], i & 0xFF, 32);
    }
    CHECK(memp_used(&m) == 100);

    for (i = 0; i < 100; i++)
    { /* 校验内容 */
        unsigned char *c = (unsigned char *)p[i];
        int j;
        for (j = 0; j < 32; j++)
            CHECK(c[j] == (unsigned char)(i & 0xFF));
    }
    for (i = 0; i < 100; i++)
        CHECK(memp_free(&m, p[i]) == 0);
    CHECK(memp_used(&m) == 0);

    memp_destroy(&m);
    return 1;
}

/* 测试 2：地址复用 + 对齐 */
static int test_reuse_align(void)
{
    mempool_t m;
    CHECK(memp_create(&m, 24) == 0);

    void *a = memp_alloc(&m);
    void *b = memp_alloc(&m);
    CHECK((uintptr_t)a % sizeof(void *) == 0); /* 8 字节对齐 */
    CHECK(memp_free(&m, b) == 0);
    CHECK(memp_free(&m, a) == 0);

    void *c = memp_alloc(&m);
    void *d = memp_alloc(&m);
    CHECK(c == a && d == b); /* LIFO 复用 */

    memp_destroy(&m);
    return 1;
}

/* 测试 3：自动扩容（一页 32 字节块约 127 个，分配 1000 块必然跨页） */
static int test_grow(void)
{
    mempool_t m;
    CHECK(memp_create(&m, 32) == 0);
    size_t one_page = m.blocks_per_page;

    void *p[1000];
    int i;
    for (i = 0; i < 1000; i++)
    {
        p[i] = memp_alloc(&m);
        CHECK(p[i] != NULL);
        memset(p[i], 0x5A, 32);
    }
    CHECK(m.page_count >= 1000 / one_page + 1); /* 确实扩了多页 */
    CHECK(memp_used(&m) == 1000);

    for (i = 0; i < 1000; i++)
        CHECK(memp_free(&m, p[i]) == 0);

    memp_destroy(&m);
    printf("      (block=32, 每页 %zu 块, 分配 1000 块用了 %zu 页)\n",
           one_page, (size_t)(1000 / one_page + 1));
    return 1;
}

/* 测试 4：memp_reset 一键回收后可继续用 */
static int test_reset(void)
{
    mempool_t m;
    CHECK(memp_create(&m, 64) == 0);

    void *p[200];
    int i;
    for (i = 0; i < 200; i++)
        p[i] = memp_alloc(&m);
    CHECK(memp_used(&m) == 200);

    memp_reset(&m);
    CHECK(memp_used(&m) == 0);

    for (i = 0; i < 200; i++)
    { /* reset 后照常分配 */
        p[i] = memp_alloc(&m);
        CHECK(p[i] != NULL);
    }
    memp_destroy(&m);
    return 1;
}

#ifdef MEMPOOL_DEBUG
/* 测试 5：调试模式下检测重复释放 */
static int test_double_free(void)
{
    mempool_t m;
    CHECK(memp_create(&m, 32) == 0);
    void *p = memp_alloc(&m);
    CHECK(memp_free(&m, p) == 0);
    CHECK(memp_free(&m, p) == -2); /* 第二次必须被发现 */
    memp_destroy(&m);
    return 1;
}
#endif

/* 性能对比：常驻 512 块，随机换血 */
static void bench(void)
{
    const int ROUNDS = 20000, LIVE = 512;
    volatile long sink = 0;

    { /* malloc/free 基线 */
        void *live[LIVE];
        int n = 0, r;
        size_t i;
        srand(42);
        clock_t t0 = clock();
        for (r = 0; r < ROUNDS; r++)
        {
            while (n < LIVE)
            {
                live[n] = malloc(32);
                sink += (long)(uintptr_t)live[n] % 7;
                n++;
            }
            for (i = 0; i < (size_t)n; i++)
                if (rand() % 2)
                {
                    free(live[i]);
                    live[i] = live[--n];
                }
        }
        while (n)
            free(live[--n]);
        printf("  malloc/free: %.1f ms\n", (double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
    }
    { /* mempool */
        mempool_t m;
        memp_create(&m, 32);
        void *live[LIVE];
        int n = 0, r;
        size_t i;
        srand(42);
        clock_t t0 = clock();
        for (r = 0; r < ROUNDS; r++)
        {
            while (n < LIVE)
            {
                live[n] = memp_alloc(&m);
                sink += (long)(uintptr_t)live[n] % 7;
                n++;
            }
            for (i = 0; i < (size_t)n; i++)
                if (rand() % 2)
                {
                    memp_free(&m, live[i]);
                    live[i] = live[--n];
                }
        }
        while (n)
            memp_free(&m, live[--n]);
        printf("  mempool    : %.1f ms\n", (double)(clock() - t0) / CLOCKS_PER_SEC * 1000);
        memp_destroy(&m);
    }
    (void)sink;
}

int main(void)
{
    printf("========== 定长内存池（C 版）验证 ==========\n");
    printf("[测试1] 基本分配/读写/释放 : %s\n", test_basic() ? "PASS" : "FAIL");
    printf("[测试2] 地址复用/对齐      : %s\n", test_reuse_align() ? "PASS" : "FAIL");
    printf("[测试3] 池满自动扩容       : %s\n", test_grow() ? "PASS" : "FAIL");
    printf("[测试4] reset 一键回收     : %s\n", test_reset() ? "PASS" : "FAIL");
#ifdef MEMPOOL_DEBUG
    printf("[测试5] 重复释放检测(DEBUG): %s\n", test_double_free() ? "PASS" : "FAIL");
#else
    printf("[测试5] 重复释放检测       : SKIP (加 -DMEMPOOL_DEBUG 编译后启用)\n");
#endif
    printf("[性能] 常驻 512 块 + 随机换血（2 万轮）:\n");
    bench();
    return 0;
}