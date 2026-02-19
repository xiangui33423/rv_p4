// test_framework.h
// 极简 C 测试框架，无外部依赖
// 用于 RV-P4 控制面固件单元测试

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

// 全局计数（在 test_main.c 中定义）
extern int g_pass;
extern int g_fail;

// ─────────────────────────────────────────────
// 宏接口
// ─────────────────────────────────────────────

/*
 * TEST_BEGIN(name) — 开始一个测试用例
 *   在函数体中使用，创建局部变量 _f（fail count）
 */
#define TEST_BEGIN(name)                                  \
    const char *_tname __attribute__((unused)) = (name); \
    int _f = 0;                                           \
    printf("  %-58s", name);                              \
    fflush(stdout)

/*
 * TEST_END() — 结束测试用例，统计结果
 */
#define TEST_END()                         \
    do {                                   \
        if (_f == 0) {                     \
            printf("PASS\n");              \
            g_pass++;                      \
        } else {                           \
            g_fail++;                      \
        }                                  \
    } while (0)

/*
 * TEST_ASSERT(cond) — 断言条件为真
 *   失败时打印文件名、行号、表达式
 */
#define TEST_ASSERT(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            if (_f == 0) printf("FAIL\n");                              \
            printf("    [ASSERT] %s:%d  %s\n", __FILE__, __LINE__,     \
                   #cond);                                              \
            _f++;                                                        \
        }                                                               \
    } while (0)

#define TEST_ASSERT_EQ(a, b)      TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b)      TEST_ASSERT((a) != (b))
#define TEST_ASSERT_NULL(p)       TEST_ASSERT((p) == NULL)
#define TEST_ASSERT_NOTNULL(p)    TEST_ASSERT((p) != NULL)
#define TEST_ASSERT_OK(ret)       TEST_ASSERT((ret) == HAL_OK)
#define TEST_ASSERT_MEM_EQ(a,b,n) TEST_ASSERT(memcmp((a),(b),(n)) == 0)

/*
 * TEST_SUITE(name) — 打印测试套件标题
 */
#define TEST_SUITE(name) \
    printf("\n=== %s ===\n", name)

#endif /* TEST_FRAMEWORK_H */
