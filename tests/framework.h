#ifndef CAGE_TEST_FRAMEWORK_H
#define CAGE_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

typedef struct {
  int passed;
  int failed;
  int total;
} test_state_t;

extern test_state_t test_state;
extern jmp_buf test_jmp_buf;

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        long long _a = (long long)(a); \
        long long _b = (long long)(b); \
        if (_a != _b) { \
            fprintf(stderr, "  FAIL %s:%d: expected %lld, got %lld\n", \
                    __FILE__, __LINE__, _b, _a); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            fprintf(stderr, "  FAIL %s:%d: %lld == %lld\n", \
                    __FILE__, __LINE__, (long long)(a), (long long)(b)); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define ASSERT_NULL(p) \
    do { \
        if ((p) != NULL) { \
            fprintf(stderr, "  FAIL %s:%d: expected NULL\n", __FILE__, __LINE__); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            fprintf(stderr, "  FAIL %s:%d: expected non-NULL\n", __FILE__, __LINE__); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", \
                    __FILE__, __LINE__, (a), (b)); \
            longjmp(test_jmp_buf, 1); \
        } \
    } while (0)

#define RUN_TEST(name) \
    do { \
        test_state.total++; \
        if (setjmp(test_jmp_buf) == 0) { \
            test_##name(); \
            printf("  \xe2\x9c\x93 %s\n", #name); \
            test_state.passed++; \
        } else { \
            printf("  \xe2\x9c\x97 %s\n", #name); \
            test_state.failed++; \
        } \
    } while (0)

#define SUITE(name) void suite_##name(void)

int test_run_suite(const char *name, void (*suite_fn)(void));
int test_summary(void);

typedef struct {
  int call_count;
  void *last_args;
  size_t args_size;
} mock_t;

void mock_init(mock_t * m);
int mock_called(const mock_t * m);
void mock_record(mock_t * m, const void *args, size_t size);
void mock_reset(mock_t * m);

#endif
