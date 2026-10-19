#include "ext4/ext4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
  const char *img = "/tmp/cage-intg-ext4.img";

  unlink(img);

  if (ext4_create_img(img) != 0) {
    fprintf(stderr, "FAIL: ext4_create_img failed\n");
    return 1;
  }

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "/sbin/e2fsck -fn %s 2>&1", img);
  int ret = system(cmd);

  unlink(img);

  if (ret != 0) {
    fprintf(stderr, "FAIL: e2fsck found errors (exit code %d)\n", ret);
    return 1;
  }

  printf("PASS: ext4 image verified by e2fsck\n");
  return 0;
}
