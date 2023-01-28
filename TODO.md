* Reorganise code addressing single responsibility, separating system dependent calls in adapters
* Fork using callback, to support different solutions, such as threads and clone
* Try hide root, unshare mount, tmpfs mount, chdir, delete dir outside, relative mount
* Investigate problems forwarding fds, background with nohup setsid fail with init
* Refactor mounts to not concatenate volumes
* Refactor launch to exec only one user per time, separating init script in another call
* Refactor user to obtain only one user data, include default groups
