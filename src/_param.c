#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "_config_default.h"

static void print_version();
static void print_help();

char *dir = NULL;
char *lower = NULL;
char *setup = NULL;
char *name = NULL;
char *user = NULL;
char **launch = NULL;
char unload = 0;
char hardcp = 0;
char shared = 0;

char param_parse(int argc, char* argv[]) {
	char moreargs = 1;
	char found;
	for (int i = 1; i < argc; i++) {
		if (moreargs) {
			found = 1;
			if (strcmp("--", argv[i]) == 0) {
				moreargs = 0;
				found = 0;
				continue;
			} else if (strcmp("-", argv[i]) == 0 && !unload) {
				unload = 1;
			} else if (strcmp("--version", argv[i]) == 0) {
				print_version();
				return 1;
			} else if (strcmp("--help", argv[i]) == 0) {
				print_help(argv[0]);
				return 1;
			} else if (strcmp("-c", argv[i]) == 0 && !hardcp) {
				hardcp = 1;
			} else if (strcmp("-p", argv[i]) == 0 && !shared) {
				shared = 1;
			} else {
				if (i + 1 >= argc) {
					found = 0;
				} else if (strcmp("-d", argv[i]) == 0) {
					dir = argv[i + 1];
					i++;
				} else if (strcmp("-r", argv[i]) == 0) {
					lower = argv[i + 1];
					i++;
				} else if (strcmp("-s", argv[i]) == 0) {
					setup = argv[i + 1];
					i++;
				} else if (strcmp("-n", argv[i]) == 0) {
					name = argv[i + 1];
					i++;
				} else if (strcmp("-u", argv[i]) == 0) {
					user = argv[i + 1];
					i++;
				} else {
					found = 0;
				}
				if (!found && (name == NULL || *name == 0)) {
					name = argv[i];
					found = 1;
				}
				if (!found) {
					moreargs = 0;
				}
			}
		}
		if (!found) {
			size_t size = sizeof(char*) * (argc - i + 1);
			launch = (char**)malloc(size);
			memcpy(launch, &argv[i], size);
			break;
		}
	}
	return 0;
}

static void print_version() {
	printf("micro-container version %s\n", VERSION);
}

static void print_help(char* self) {
	printf("Usage: %s [-d library-path] [-r lower-root] [-c] [-e] "
		"[-s setup-script] [-n container-name] [-u username] "
		"[-- [launch-command]]\n", self);
}