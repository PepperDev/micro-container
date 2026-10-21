#include "args.h"
// cppcheck-suppress missingIncludeSystem
#include <stdio.h>
// cppcheck-suppress missingIncludeSystem
#include <string.h>
// cppcheck-suppress missingIncludeSystem
#include <unistd.h>
// cppcheck-suppress missingIncludeSystem
#include <getopt.h>

static struct option long_opts[] = {
  {"appdir", required_argument, NULL, 'a'},
  {"cgroup", no_argument, NULL, 'C'},
  {"cwd", required_argument, NULL, 'c'},
  {"env", required_argument, NULL, 'e'},
  {"gui", no_argument, NULL, 'G'},
  {"group", required_argument, NULL, 'g'},
  {"help", no_argument, NULL, 'h'},
  {"init", required_argument, NULL, 'i'},
  {"kill", no_argument, NULL, 'k'},
  {"lower", required_argument, NULL, 'l'},
  {"net", no_argument, NULL, 'N'},
  {"name", required_argument, NULL, 'n'},
  {"pidfile", required_argument, NULL, 'p'},
  {"upper", required_argument, NULL, 'U'},
  {"user", required_argument, NULL, 'u'},
  {"version", no_argument, NULL, 'V'},
  {"volume", required_argument, NULL, 'v'},
  {"work", required_argument, NULL, 'w'},
  {0, 0, 0, 0}
};

typedef int (*opt_fn)(cage_config_t *);

static int handle_a(cage_config_t *cfg)
{
  cfg->appdir = optarg;
  return 0;
}

static int handle_C(cage_config_t *cfg)
{
  cfg->unshare_cgroup = 1;
  return 0;
}

static int handle_c(cage_config_t *cfg)
{
  cfg->currentdir = optarg;
  return 0;
}

static int handle_e(cage_config_t *cfg)
{
  if (cfg->envvar_count >= CAGE_MAX_ENVVARS) {
    fprintf(stderr, "cage: too many -e options\n");
    return -1;
  }
  cfg->envvars[cfg->envvar_count++] = optarg;
  return 0;
}

static int handle_G(cage_config_t *cfg)
{
  cfg->graphical = 1;
  return 0;
}

static int handle_g(cage_config_t *cfg)
{
  cfg->group = optarg;
  return 0;
}

static int handle_i(cage_config_t *cfg)
{
  cfg->initscript = optarg;
  return 0;
}

static int handle_k(cage_config_t *cfg)
{
  cfg->kill_mode = 1;
  return 0;
}

static int handle_l(cage_config_t *cfg)
{
  cfg->lowerdir = optarg;
  return 0;
}

static int handle_N(cage_config_t *cfg)
{
  cfg->unshare_net = 1;
  return 0;
}

static int handle_n(cage_config_t *cfg)
{
  if (optarg[0] == '\0' || strchr(optarg, '/') != NULL) {
    fprintf(stderr, "cage: -n expects a non-empty name without '/'\n");
    return -1;
  }
  cfg->name = optarg;
  return 0;
}

static int handle_p(cage_config_t *cfg)
{
  cfg->pidfile = optarg;
  return 0;
}

static int handle_U(cage_config_t *cfg)
{
  cfg->upperdir = optarg;
  return 0;
}

static int handle_u(cage_config_t *cfg)
{
  cfg->user = optarg;
  return 0;
}

static int handle_v(cage_config_t *cfg)
{
  if (cfg->volume_count >= CAGE_MAX_VOLUMES) {
    fprintf(stderr, "cage: too many -v options\n");
    return -1;
  }
  cfg->volumes[cfg->volume_count++] = optarg;
  return 0;
}

static int handle_w(cage_config_t *cfg)
{
  cfg->workdir = optarg;
  return 0;
}

static const opt_fn opt_handlers[256] = {
  ['a'] = handle_a,['C'] = handle_C,['c'] = handle_c,
  ['e'] = handle_e,['G'] = handle_G,['g'] = handle_g,
  ['i'] = handle_i,['k'] = handle_k,['l'] = handle_l,
  ['N'] = handle_N,['n'] = handle_n,['p'] = handle_p,
  ['U'] = handle_U,['u'] = handle_u,['v'] = handle_v,
  ['w'] = handle_w,
};

static int handle_normal(cage_config_t *cfg, int opt)
{
  opt_fn fn = opt_handlers[(unsigned char)opt];
  if (!fn)
    return 0;
  return fn(cfg);
}

static void report_unknown(int optopt, char **argv)
{
  if (optopt != 0)
    fprintf(stderr, "cage: unknown option -%c\n", optopt);
  else
    fprintf(stderr, "cage: unknown option %s\n", argv[optind - 1]);
}

static int handle_opt(cage_config_t *cfg, int opt, int optopt, char **argv)
{
  switch (opt) {
  case 'h':
    cfg->show_help = 1;
    return 0;
  case 'V':
    cfg->show_version = 1;
    return 0;
  case ':':
    fprintf(stderr, "cage: option requires an argument\n");
    return -1;
  case '?':
    report_unknown(optopt, argv);
    return -1;
  default:
    return handle_normal(cfg, opt);
  }
}

void cage_config_init(cage_config_t *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->currentdir = "/";
  cfg->lowerdir = "/";
  cfg->group = NULL;
  cfg->user = "0";
}

static int collect_commands(cage_config_t *cfg, int argc, char **argv, int start)
{
  for (int i = start; i < argc; i++) {
    if (cfg->command_count >= CAGE_MAX_CMD_ARGS) {
      fprintf(stderr, "cage: too many command arguments\n");
      return -1;
    }
    cfg->command[cfg->command_count++] = argv[i];
  }
  return 0;
}

int cage_parse_args(cage_config_t *cfg, int argc, char **argv)
{
  int opt;
  optind = 1;
  opterr = 0;
  int prev = optind;
  while ((opt = getopt_long(argc, argv, "+:a:Cc:e:Gg:hi:kl:Nn:p:U:u:Vv:w:", long_opts, NULL)) != -1) {
    prev = optind;
    int r = handle_opt(cfg, opt, optopt, argv);
    if (r != 0)
      return r;
  }
  cfg->has_dashdash = (prev >= 1 && prev < argc && optind == prev + 1 && strcmp(argv[prev], "--") == 0);
  return collect_commands(cfg, argc, argv, optind);
}

void cage_print_usage(void)
{
  printf("Usage: cage [options...] [--] [command...]\n"
         "\n"
         "Options:\n"
         "  -a, --appdir <dir>     Application base directory\n"
         "  -C, --cgroup           Unshare cgroup namespace\n"
         "  -c, --cwd <dir>        Working directory inside container\n"
         "                         (default: /)\n"
         "  -e, --env <key=value>  Add environment variable\n"
         "  -G, --gui              Setup environment for GUI apps\n"
         "  -g, --group <group>    Group name or GID (default: 0)\n"
         "  -h, --help             Print help and exit\n"
         "  -i, --init <file>      Init script to run as root\n"
         "  -k, --kill             Stop/kill the running instance\n"
         "  -l, --lower <dir>      OverlayFS lower directory (default: /)\n"
         "  -N, --net              Unshare network namespace\n"
         "  -n, --name <name>      Application name\n"
         "  -p, --pidfile <file>   PID file path\n"
         "  -U, --upper <dir>      OverlayFS upper directory\n"
         "  -u, --user <user>      User name or UID (default: 0)\n"
         "  -V, --version          Print version and exit\n"
         "  -v, --volume <h:ct>    Map host:container volume\n" "  -w, --work <dir>       OverlayFS work directory\n");
}

void cage_print_version(void)
{
  printf("cage version %s\n", CAGE_VERSION);
}
