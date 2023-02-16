* Bug: environment variable for -G is not being exported when rerunning existing ns
* Bug: loop may achieve its limits, check if it's the case and ask for more, that is what was blocking it before
* Bug: automatically detect when overlay is not present at /proc/filesystems and modprobe it
* Bug: install first cgroup if non present /proc/cgroups: mount -t cgroup xxx /sys/fs/cgroup
* Reorganise code addressing single responsibility, separating system dependent calls in adapters
* Fork using callback, to support different solutions, such as threads and clone
* Try hide root: fork before unshare mount, delete dir outside unshare after chroot, try relave overlay opts, use pipe to ipc
* If init script doesn't exists with root directory as prefix open file in the host before and replace shell parameter
* Refactor mounts to not concatenate volumes
* Refactor launch to exec only one user per time, separating init script in another call
* Refactor user to obtain only one user data, include default groups
