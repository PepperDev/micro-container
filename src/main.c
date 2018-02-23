#include <sched.h>
#include <string.h>
#include <stdlib.h>

// default: /var/lib/micro-container
char *dir = NULL;
// default: $dir/root-$name or /
char *lower = NULL;
char *setup = NULL;
char *name = NULL;
char *user = NULL;
char **launch = NULL;
// TODO: volumes, x11, pulse
char unload = 0;
// TODO: allow not using overlay by -n option, it should copy the root

// /proc/self/mountinfo \040 -> space
// mount , -> \,

int main(int argc, char* argv[]) {
	char moreargs = 1;
	char found;
	for (int i = 0; i < argc; i++) {
		if (moreargs) {
			found = 1;
			if (strcmp("--", argv[i]) == 0) {
				moreargs = 0;
				found = 0;
				continue;
			} else if (strcmp("-", argv[i]) == 0 && !unload) {
				unload = 1;
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
	//clone2
	//execve
	//setns(int fd, int nstype);
	return 0;
}
