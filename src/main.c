#include "cli/args.h"
#include "core/cage.h"
#include "core/proc.h"
#include "core/reuse.h"
#include "core/super.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <stdlib.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>

static int handle_fix(int argc, char **argv)
{
  return argc == 2 && strcmp(argv[1], "--fix") == 0 ? super_fix() != 0 : -1;
}

static int handle_config_exits(const cage_config_t *cfg)
{
  if (cfg->show_help) {
    cage_print_usage();
    return 0;
  }
  if (cfg->show_version) {
    cage_print_version();
    return 0;
  }
  if (cfg->kill_mode)
    return killpid(cfg->name, cfg->pidfile) != 0;
  return -1;
}

static int do_spawn(cage_config_t *cfg, uid_t ruid)
{
  pid_t existing = detect_existing(cfg->name, cfg->pidfile);
  if (existing < 0)
    return 1;
  int r;
  if (existing > 0)
    r = spawn_existing(cfg, existing, ruid);
  else
    r = spawn_cage(cfg, ruid);
  return r < 0 ? 1 : r;
}

static int check_no_command(const cage_config_t *cfg)
{
  if (cfg->command_count > 0)
    return 0;
  if (cfg->has_dashdash)
    return 0;
  fprintf(stderr, "cage: no command specified\n");
  cage_print_usage();
  return 1;
}

static void get_ids(uid_t *euid, uid_t *ruid, gid_t *egid, gid_t *rgid)
{
  uid_t suid;
  gid_t sgid;
  getresuid(ruid, euid, &suid);
  getresgid(rgid, egid, &sgid);
}

int main(int argc, char **argv)
{
  int r = handle_fix(argc, argv);
  if (r >= 0)
    return r;
  uid_t euid, ruid;
  gid_t egid, rgid;
  get_ids(&euid, &ruid, &egid, &rgid);
  if (super_escalate(argv, euid, ruid, egid, rgid) != 0)
    return 1;
  cage_config_t cfg;
  cage_config_init(&cfg);
  if (cage_parse_args(&cfg, argc, argv) != 0)
    return 1;
  r = handle_config_exits(&cfg);
  if (r >= 0)
    return r;
  if (check_no_command(&cfg))
    return 1;
  return do_spawn(&cfg, ruid);
}
