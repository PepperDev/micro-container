#include "_param.h"
#include "_mount.h"
#include "_credential.h"

int main(int argc, char* argv[]) {
	check_credential(argc, argv);
	if (param_parse(argc, argv)) {
		return 0;
	}
	mount_container();
	return 0;
}
