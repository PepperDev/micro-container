#include "framework.h"
#include "test_util.h"
#include "user/user.h"
#include "util/io.h"
#include "util/mem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void setup_rootfs(const char *root, const char *passwd, const char *group)
{
  char etc[512], pw[512], gr[512];
  snprintf(etc, sizeof(etc), "%s/etc", root);
  snprintf(pw, sizeof(pw), "%s/etc/passwd", root);
  snprintf(gr, sizeof(gr), "%s/etc/group", root);

  /* ensure root exists */
  mkdir(root, 0755);
  mkdir(etc, 0755);

  FILE *f = fopen(pw, "w");
  if (f) {
    fprintf(f, "%s", passwd);
    fclose(f);
  }

  if (group) {
    FILE *g = fopen(gr, "w");
    if (g) {
      fprintf(g, "%s", group);
      fclose(g);
    }
  }
}

static void test_numeric_uid_with_passwd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/bash\n" "bob:x:1001:1001:Bob:/home/bob:/bin/sh\n", NULL);

  user_t *u = user_new("1000", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 1000);
  ASSERT_STR_EQ(get_name(u), "alice");
  ASSERT_STR_EQ(get_home(u), "/home/alice");
  ASSERT_STR_EQ(get_shell(u), "/bin/bash");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_username_lookup(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n" "alice:x:1000:1000:Alice:/home/alice:/bin/zsh\n", NULL);

  user_t *u = user_new("alice", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 1000);
  ASSERT_STR_EQ(get_name(u), "alice");
  ASSERT_STR_EQ(get_home(u), "/home/alice");
  ASSERT_STR_EQ(get_shell(u), "/bin/zsh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_user_colon_group(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\n");

  user_t *u = user_new("1000:500", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_STR_EQ(get_name(u), "alice");
  ASSERT_EQ(get_gid(u), 500);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_group_name_lookup(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\ndocker:x:999:\n");

  user_t *u = user_new("alice:staff", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 500);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_supplementary_groups(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\ndocker:x:999:\n");

  user_t *u = user_new("1000", "500:999", root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 500);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 2);
  ASSERT_EQ(gl.groups[0], 500);
  ASSERT_EQ(gl.groups[1], 999);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_primary_group_from_group_str(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\n");

  user_t *u = user_new("1000", "500", root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 500);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 1);
  ASSERT_EQ(gl.groups[0], 500);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_numeric_uid_with_group_token_forces_passwd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/zsh\n", "staff:x:500:\n");

  user_t *u = user_new("1000", "500", root);
  ASSERT_NOT_NULL(u);
  user_resolve(u);

  /* simulate chroot: the host-side ${rootfs}/etc/passwd is no longer reachable */
  char pw[512];
  snprintf(pw, sizeof(pw), "%s/etc/passwd", root);
  io_unlink(pw);

  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_STR_EQ(get_name(u), "alice");
  ASSERT_STR_EQ(get_home(u), "/home/alice");
  ASSERT_STR_EQ(get_shell(u), "/bin/zsh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_no_passwd_file(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  char *nonexistent = mem_path(root, "nonexistent");
  user_t *u = user_new("1000", NULL, nonexistent);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 1000);
  ASSERT_NULL(get_name(u));
  ASSERT_STR_EQ(get_home(u), "/home/.user");
  ASSERT_STR_EQ(get_shell(u), "/bin/sh");
  user_free(u);
  mem_free(nonexistent);
  test_rmtree(root);
  mem_free(root);
}

static void test_empty_user_str(void)
{
  user_t *u = user_new("", NULL, NULL);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 0);
  ASSERT_EQ(get_gid(u), 0);
  user_free(u);
}

static void test_uid_overflow_rejected(void)
{
  user_t *u = user_new("4294967296", NULL, NULL);
  ASSERT_NULL(u);
}

static void test_uid_way_overflow_rejected(void)
{
  user_t *u = user_new("99999999999999999999", NULL, NULL);
  ASSERT_NULL(u);
}

static void test_gid_overflow_rejected(void)
{
  user_t *u = user_new("0", "4294967296", NULL);
  ASSERT_NULL(u);
}

static void test_uid_max_accepted(void)
{
  user_t *u = user_new("4294967295", NULL, NULL);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 4294967295u);
  user_free(u);
}

static void test_numeric_uid_not_in_passwd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n" "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", NULL);

  user_t *u = user_new("2000", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 2000);
  ASSERT_EQ(get_gid(u), 2000);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 1);
  ASSERT_EQ(gl.groups[0], 2000);
  ASSERT_NULL(get_name(u));
  ASSERT_STR_EQ(get_home(u), "/home/.user");
  ASSERT_STR_EQ(get_shell(u), "/bin/sh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_root_user(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n", NULL);

  user_t *u = user_new("0", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 0);
  ASSERT_EQ(get_gid(u), 0);
  ASSERT_STR_EQ(get_name(u), "root");
  ASSERT_STR_EQ(get_home(u), "/root");
  ASSERT_STR_EQ(get_shell(u), "/bin/sh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_root_user_no_passwd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  io_mkdir(root);

  user_t *u = user_new("0", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 0);
  ASSERT_EQ(get_gid(u), 0);
  ASSERT_STR_EQ(get_name(u), "root");
  ASSERT_STR_EQ(get_home(u), "/root");
  ASSERT_STR_EQ(get_shell(u), "/bin/sh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_supplementary_with_group_names(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\ndocker:x:999:\n");

  user_t *u = user_new("1000", "staff:docker", root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 500);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 2);
  ASSERT_EQ(gl.groups[0], 500);
  ASSERT_EQ(gl.groups[1], 999);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_user_colon_and_supplementary(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\ndocker:x:999:\nadm:x:1001:\n");

  user_t *u = user_new("1000:500", "999:1001", root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 999);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 3);
  ASSERT_EQ(gl.groups[0], 500);
  ASSERT_EQ(gl.groups[1], 999);
  ASSERT_EQ(gl.groups[2], 1001);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_user_colon_with_supplementary(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\ndocker:x:999:\nadm:x:1002:\n");

  user_t *u = user_new("1001:1001:1002", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1001);
  ASSERT_EQ(get_gid(u), 1001);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 2);
  ASSERT_EQ(gl.groups[0], 1001);
  ASSERT_EQ(gl.groups[1], 1002);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_invalid_group_name(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:\n");

  user_t *u = user_new("alice:nonexistent", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 1000);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_user_not_found_by_name(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\n", NULL);

  user_t *u = user_new("nobody", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 65534);
  ASSERT_EQ(get_gid(u), 65534);
  ASSERT_STR_EQ(get_name(u), "nobody");
  ASSERT_STR_EQ(get_home(u), "/home/.user");
  ASSERT_STR_EQ(get_shell(u), "/bin/sh");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_not_found_by_name_nobody_in_passwd(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root, "root:x:0:0:root:/root:/bin/sh\nnobody:x:999:999:Nobody:/nonexistent:/bin/false\n", NULL);

  user_t *u = user_new("ghost", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 999);
  ASSERT_EQ(get_gid(u), 999);
  ASSERT_STR_EQ(get_name(u), "ghost");
  ASSERT_STR_EQ(get_home(u), "/nonexistent");
  ASSERT_STR_EQ(get_shell(u), "/bin/false");
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_name_no_groups_enumerate(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root,
               "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1001:Alice:/home/alice:/bin/sh\n",
               "staff:x:500:alice,bob\n" "sudo:x:27:alice\n" "docker:x:999:bob\n" "emptygrp:x:111:\n");
  user_t *u = user_new("alice", NULL, root);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_gid(u), 1001);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 3);
  int found_primary = 0, found_staff = 0, found_sudo = 0;
  for (size_t i = 0; i < gl.count; i++) {
    if (gl.groups[i] == 1001)
      found_primary = 1;
    if (gl.groups[i] == 500)
      found_staff = 1;
    if (gl.groups[i] == 27)
      found_sudo = 1;
  }
  ASSERT_EQ(found_primary, 1);
  ASSERT_EQ(found_staff, 1);
  ASSERT_EQ(found_sudo, 1);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_name_groups_after_early_resolve(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root,
               "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1001:Alice:/home/alice:/bin/sh\n", "staff:x:500:alice,bob\n" "sudo:x:27:alice\n");
  user_t *u = user_new("alice", NULL, root);
  ASSERT_NOT_NULL(u);
  /* eager order: passwd resolved before any group getter, like eager_resolve_user */
  user_resolve(u);
  ASSERT_EQ(get_uid(u), 1000);
  ASSERT_EQ(get_gid(u), 1001);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 3);
  ASSERT_EQ(gl.groups[0], 27);
  ASSERT_EQ(gl.groups[1], 500);
  ASSERT_EQ(gl.groups[2], 1001);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_numeric_uid_no_enum_after_resolve(void)
{
  char *root = test_temp_root();
  ASSERT_NOT_NULL(root);
  setup_rootfs(root,
               "root:x:0:0:root:/root:/bin/sh\n"
               "alice:x:1000:1000:Alice:/home/alice:/bin/sh\n", "staff:x:500:alice\n" "sudo:x:27:alice\n");
  user_t *u = user_new("1000", NULL, root);
  ASSERT_NOT_NULL(u);
  user_resolve(u);
  group_list_t gl = get_groups(u);
  ASSERT_EQ(gl.count, 1);
  ASSERT_EQ(gl.groups[0], 1000);
  user_free(u);
  test_rmtree(root);
  mem_free(root);
}

static void test_root_new(void)
{
  user_t *u = user_root_new(NULL);
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 0);
  ASSERT_EQ(get_gid(u), 0);
  user_free(u);
}

static void test_root_new_with_rootfs(void)
{
  user_t *u = user_root_new("/tmp/root");
  ASSERT_NOT_NULL(u);
  ASSERT_EQ(get_uid(u), 0);
  ASSERT_EQ(get_gid(u), 0);
  ASSERT_STR_EQ(get_rootfs(u), "/tmp/root");
  user_free(u);
}

static void test_new_invalid_long_name(void)
{
  char name[300];
  memset(name, 'a', 299);
  name[299] = '\0';
  user_t *u = user_new(name, NULL, NULL);
  ASSERT_NULL(u);
}

static void test_free_null(void)
{
  user_free(NULL);
}

SUITE(user)
{
  RUN_TEST(numeric_uid_with_passwd);
  RUN_TEST(username_lookup);
  RUN_TEST(user_colon_group);
  RUN_TEST(group_name_lookup);
  RUN_TEST(supplementary_groups);
  RUN_TEST(primary_group_from_group_str);
  RUN_TEST(numeric_uid_with_group_token_forces_passwd);
  RUN_TEST(no_passwd_file);
  RUN_TEST(empty_user_str);
  RUN_TEST(uid_overflow_rejected);
  RUN_TEST(uid_way_overflow_rejected);
  RUN_TEST(gid_overflow_rejected);
  RUN_TEST(uid_max_accepted);
  RUN_TEST(numeric_uid_not_in_passwd);
  RUN_TEST(root_user);
  RUN_TEST(root_user_no_passwd);
  RUN_TEST(supplementary_with_group_names);
  RUN_TEST(user_not_found_by_name);
  RUN_TEST(not_found_by_name_nobody_in_passwd);
  RUN_TEST(user_colon_and_supplementary);
  RUN_TEST(user_colon_with_supplementary);
  RUN_TEST(invalid_group_name);
  RUN_TEST(name_no_groups_enumerate);
  RUN_TEST(name_groups_after_early_resolve);
  RUN_TEST(numeric_uid_no_enum_after_resolve);
  RUN_TEST(root_new);
  RUN_TEST(root_new_with_rootfs);
  RUN_TEST(new_invalid_long_name);
  RUN_TEST(free_null);
}
