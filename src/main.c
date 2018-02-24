#include "param.h"
#include "mount.h"

int main(int argc, char* argv[]) {
	if (param_parse(argc, argv)) {
		return 0;
	}
	mount_container();
	return 0;
}
