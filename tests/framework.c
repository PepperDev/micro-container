#include "framework.h"

test_state_t test_state = { 0, 0, 0 };

jmp_buf test_jmp_buf;

int test_run_suite(const char *name, void (*suite_fn)(void))
{
  printf("\n== %s ==\n", name);
  suite_fn();
  return 0;
}

int test_summary(void)
{
  printf("\n== Results: %d/%d passed, %d failed ==\n", test_state.passed, test_state.total, test_state.failed);
  return test_state.failed > 0 ? 1 : 0;
}

void mock_init(mock_t *m)
{
  m->call_count = 0;
  m->last_args = NULL;
  m->args_size = 0;
}

int mock_called(const mock_t *m)
{
  return m->call_count;
}

void mock_record(mock_t *m, const void *args, size_t size)
{
  m->call_count++;
  free(m->last_args);
  m->last_args = NULL;
  m->args_size = 0;
  if (size > 0 && args != NULL) {
    m->last_args = malloc(size);
    if (m->last_args != NULL) {
      memcpy(m->last_args, args, size);
      m->args_size = size;
    }
  }
}

void mock_reset(mock_t *m)
{
  free(m->last_args);
  mock_init(m);
}
