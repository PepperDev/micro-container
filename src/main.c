#include "param.h"

// /proc/self/mountinfo \040 -> space
// mount , -> \,

int main(int argc, char* argv[]) {
	if (param_parse(argc, argv)) {
		return 0;
	}
	//clone2
	//execve
	//setns(int fd, int nstype);
	return 0;
}
