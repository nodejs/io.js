# OS

<!--introduced_in=v0.10.0-->

> Stability: 2 - Stable

<!-- source_link=lib/os.js -->

The `node:os` module provides operating system-related utility methods and
properties. It can be accessed using:

```mjs
import os from 'node:os';
```

```cjs
const os = require('node:os');
```

## `os.EOL`

<!-- YAML
added: v0.7.8
-->

* {string}

The operating system-specific end-of-line marker.

* `\n` on POSIX
* `\r\n` on Windows

## `os.availableParallelism()`

<!-- YAML
added:
  - v19.4.0
  - v18.14.0
-->

* Returns: {integer}

Returns an estimate of the default amount of parallelism a program should use.
Always returns a value greater than zero.

This function is a small wrapper about libuv's [`uv_available_parallelism()`][].

## `os.arch()`

<!-- YAML
added: v0.5.0
-->

* Returns: {string}

Returns the operating system CPU architecture for which the Node.js binary was
compiled. Possible values are `'arm'`, `'arm64'`, `'ia32'`, `'loong64'`,
`'mips'`, `'mipsel'`, `'ppc64'`, `'riscv64'`, `'s390x'`, and `'x64'`.

The return value is equivalent to [`process.arch`][].

## `os.constants`

<!-- YAML
added: v6.3.0
-->

* {Object}

Contains commonly used operating system-specific constants for error codes,
process signals, and so on. The specific constants defined are described in
[OS constants](#os-constants).

## `os.cpus()`

<!-- YAML
added: v0.3.3
-->

* Returns: {Object\[]}

Returns an array of objects containing information about each logical CPU core.
The array will be empty if no CPU information is available, such as if the
`/proc` file system is unavailable.

The properties included on each object include:

* `model` {string}
* `speed` {number} (in MHz)
* `times` {Object}
  * `user` {number} The number of milliseconds the CPU has spent in user mode.
  * `nice` {number} The number of milliseconds the CPU has spent in nice mode.
  * `sys` {number} The number of milliseconds the CPU has spent in sys mode.
  * `idle` {number} The number of milliseconds the CPU has spent in idle mode.
  * `irq` {number} The number of milliseconds the CPU has spent in irq mode.

<!-- eslint-disable @stylistic/js/semi -->

```js
[
  {
    model: 'Intel(R) Core(TM) i7 CPU         860  @ 2.80GHz',
    speed: 2926,
    times: {
      user: 252020,
      nice: 0,
      sys: 30340,
      idle: 1070356870,
      irq: 0,
    },
  },
  {
    model: 'Intel(R) Core(TM) i7 CPU         860  @ 2.80GHz',
    speed: 2926,
    times: {
      user: 306960,
      nice: 0,
      sys: 26980,
      idle: 1071569080,
      irq: 0,
    },
  },
  {
    model: 'Intel(R) Core(TM) i7 CPU         860  @ 2.80GHz',
    speed: 2926,
    times: {
      user: 248450,
      nice: 0,
      sys: 21750,
      idle: 1070919370,
      irq: 0,
    },
  },
  {
    model: 'Intel(R) Core(TM) i7 CPU         860  @ 2.80GHz',
    speed: 2926,
    times: {
      user: 256880,
      nice: 0,
      sys: 19430,
      idle: 1070905480,
      irq: 20,
    },
  },
]
```

`nice` values are POSIX-only. On Windows, the `nice` values of all processors
are always 0.

`os.cpus().length` should not be used to calculate the amount of parallelism
available to an application. Use
[`os.availableParallelism()`](#osavailableparallelism) for this purpose.

## `os.devNull`

<!-- YAML
added:
  - v16.3.0
  - v14.18.0
-->

* {string}

The platform-specific file path of the null device.

* `\\.\nul` on Windows
* `/dev/null` on POSIX

## `os.endianness()`

<!-- YAML
added: v0.9.4
-->

* Returns: {string}

Returns a string identifying the endianness of the CPU for which the Node.js
binary was compiled.

Possible values are `'BE'` for big endian and `'LE'` for little endian.

## `os.freemem()`

<!-- YAML
added: v0.3.3
-->

* Returns: {integer}

Returns the amount of free system memory in bytes as an integer.

## `os.getPriority([pid])`

<!-- YAML
added: v10.10.0
-->

* `pid` {integer} The process ID to retrieve scheduling priority for.
  **Default:** `0`.
* Returns: {integer}

Returns the scheduling priority for the process specified by `pid`. If `pid` is
not provided or is `0`, the priority of the current process is returned.

## `os.homedir()`

<!-- YAML
added: v2.3.0
-->

* Returns: {string}

Returns the string path of the current user's home directory.

On POSIX, it uses the `$HOME` environment variable if defined. Otherwise it
uses the [effective UID][EUID] to look up the user's home directory.

On Windows, it uses the `USERPROFILE` environment variable if defined.
Otherwise it uses the path to the profile directory of the current user.

## `os.hostname()`

<!-- YAML
added: v0.3.3
-->

* Returns: {string}

Returns the host name of the operating system as a string.

## `os.loadavg()`

<!-- YAML
added: v0.3.3
-->

* Returns: {number\[]}

Returns an array containing the 1, 5, and 15 minute load averages.

The load average is a measure of system activity calculated by the operating
system and expressed as a fractional number.

The load average is a Unix-specific concept. On Windows, the return value is
always `[0, 0, 0]`.

## `os.machine()`

<!-- YAML
added:
  - v18.9.0
  - v16.18.0
-->

* Returns: {string}

Returns the machine type as a string, such as `arm`, `arm64`, `aarch64`,
`mips`, `mips64`, `ppc64`, `ppc64le`, `s390x`, `i386`, `i686`, `x86_64`.

On POSIX systems, the machine type is determined by calling
[`uname(3)`][]. On Windows, `RtlGetVersion()` is used, and if it is not
available, `GetVersionExW()` will be used. See
<https://en.wikipedia.org/wiki/Uname#Examples> for more information.

## `os.networkInterfaces()`

<!-- YAML
added: v0.6.0
changes:
  - version: v18.4.0
    pr-url: https://github.com/nodejs/node/pull/43054
    description: The `family` property now returns a string instead of a number.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41431
    description: The `family` property now returns a number instead of a string.
-->

* Returns: {Object}

Returns an object containing network interfaces that have been assigned a
network address.

Each key on the returned object identifies a network interface. The associated
value is an array of objects that each describe an assigned network address.

The properties available on the assigned network address object include:

* `address` {string} The assigned IPv4 or IPv6 address
* `netmask` {string} The IPv4 or IPv6 network mask
* `family` {string} Either `IPv4` or `IPv6`
* `mac` {string} The MAC address of the network interface
* `internal` {boolean} `true` if the network interface is a loopback or
  similar interface that is not remotely accessible; otherwise `false`
* `scopeid` {number} The numeric IPv6 scope ID (only specified when `family`
  is `IPv6`)
* `cidr` {string} The assigned IPv4 or IPv6 address with the routing prefix
  in CIDR notation. If the `netmask` is invalid, this property is set
  to `null`.

<!-- eslint-skip -->

```js
{
  lo: [
    {
      address: '127.0.0.1',
      netmask: '255.0.0.0',
      family: 'IPv4',
      mac: '00:00:00:00:00:00',
      internal: true,
      cidr: '127.0.0.1/8'
    },
    {
      address: '::1',
      netmask: 'ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff',
      family: 'IPv6',
      mac: '00:00:00:00:00:00',
      scopeid: 0,
      internal: true,
      cidr: '::1/128'
    }
  ],
  eth0: [
    {
      address: '192.168.1.108',
      netmask: '255.255.255.0',
      family: 'IPv4',
      mac: '01:02:03:0a:0b:0c',
      internal: false,
      cidr: '192.168.1.108/24'
    },
    {
      address: 'fe80::a00:27ff:fe4e:66a1',
      netmask: 'ffff:ffff:ffff:ffff::',
      family: 'IPv6',
      mac: '01:02:03:0a:0b:0c',
      scopeid: 1,
      internal: false,
      cidr: 'fe80::a00:27ff:fe4e:66a1/64'
    }
  ]
}
```

## `os.platform()`

<!-- YAML
added: v0.5.0
-->

* Returns: {string}

Returns a string identifying the operating system platform for which
the Node.js binary was compiled. The value is set at compile time.
Possible values are `'aix'`, `'darwin'`, `'freebsd'`,`'linux'`,
`'openbsd'`, `'sunos'`, and `'win32'`.

The return value is equivalent to [`process.platform`][].

The value `'android'` may also be returned if Node.js is built on the Android
operating system. [Android support is experimental][Android building].

## `os.release()`

<!-- YAML
added: v0.3.3
-->

* Returns: {string}

Returns the operating system as a string.

On POSIX systems, the operating system release is determined by calling
[`uname(3)`][]. On Windows, `GetVersionExW()` is used. See
<https://en.wikipedia.org/wiki/Uname#Examples> for more information.

## `os.setPriority([pid, ]priority)`

<!-- YAML
added: v10.10.0
-->

* `pid` {integer} The process ID to set scheduling priority for.
  **Default:** `0`.
* `priority` {integer} The scheduling priority to assign to the process.

Attempts to set the scheduling priority for the process specified by `pid`. If
`pid` is not provided or is `0`, the process ID of the current process is used.

The `priority` input must be an integer between `-20` (high priority) and `19`
(low priority). Due to differences between Unix priority levels and Windows
priority classes, `priority` is mapped to one of six priority constants in
`os.constants.priority`. When retrieving a process priority level, this range
mapping may cause the return value to be slightly different on Windows. To avoid
confusion, set `priority` to one of the priority constants.

On Windows, setting priority to `PRIORITY_HIGHEST` requires elevated user
privileges. Otherwise the set priority will be silently reduced to
`PRIORITY_HIGH`.

## `os.tmpdir()`

<!-- YAML
added: v0.9.9
changes:
  - version: v2.0.0
    pr-url: https://github.com/nodejs/node/pull/747
    description: This function is now cross-platform consistent and no longer
                 returns a path with a trailing slash on any platform.
-->

* Returns: {string}

Returns the operating system's default directory for temporary files as a
string.

On Windows, the result can be overridden by `TEMP` and `TMP` environment variables, and
`TEMP` takes precedence over `TMP`. If neither is set, it defaults to `%SystemRoot%\temp`
or `%windir%\temp`.

On non-Windows platforms, `TMPDIR`, `TMP` and `TEMP` environment variables will be checked
to override the result of this method, in the described order. If none of them is set, it
defaults to `/tmp`.

Some operating system distributions would either configure `TMPDIR` (non-Windows) or
`TEMP` and `TMP` (Windows) by default without additional configurations by the system
administrators. The result of `os.tmpdir()` typically reflects the system preference
unless it's explicitly overridden by the users.

## `os.totalmem()`

<!-- YAML
added: v0.3.3
-->

* Returns: {integer}

Returns the total amount of system memory in bytes as an integer.

## `os.type()`

<!-- YAML
added: v0.3.3
-->

* Returns: {string}

Returns the operating system name as returned by [`uname(3)`][]. For example, it
returns `'Linux'` on Linux, `'Darwin'` on macOS, and `'Windows_NT'` on Windows.

See <https://en.wikipedia.org/wiki/Uname#Examples> for additional information
about the output of running [`uname(3)`][] on various operating systems.

## `os.uptime()`

<!-- YAML
added: v0.3.3
changes:
  - version: v10.0.0
    pr-url: https://github.com/nodejs/node/pull/20129
    description: The result of this function no longer contains a fraction
                 component on Windows.
-->

* Returns: {integer}

Returns the system uptime in number of seconds.

## `os.userInfo([options])`

<!-- YAML
added: v6.0.0
-->

* `options` {Object}
  * `encoding` {string} Character encoding used to interpret resulting strings.
    If `encoding` is set to `'buffer'`, the `username`, `shell`, and `homedir`
    values will be `Buffer` instances. **Default:** `'utf8'`.
* Returns: {Object}

Returns information about the currently effective user. On POSIX platforms,
this is typically a subset of the password file. The returned object includes
the `username`, `uid`, `gid`, `shell`, and `homedir`. On Windows, the `uid` and
`gid` fields are `-1`, and `shell` is `null`.

The value of `homedir` returned by `os.userInfo()` is provided by the operating
system. This differs from the result of `os.homedir()`, which queries
environment variables for the home directory before falling back to the
operating system response.

Throws a [`SystemError`][] if a user has no `username` or `homedir`.

## `os.version()`

<!-- YAML
added:
 - v13.11.0
 - v12.17.0
-->

* Returns: {string}

Returns a string identifying the kernel version.

On POSIX systems, the operating system release is determined by calling
[`uname(3)`][]. On Windows, `RtlGetVersion()` is used, and if it is not
available, `GetVersionExW()` will be used. See
<https://en.wikipedia.org/wiki/Uname#Examples> for more information.

## OS constants

The following constants are exported by `os.constants`.

Not all constants will be available on every operating system.

### Signal constants

<!-- YAML
changes:
  - version: v5.11.0
    pr-url: https://github.com/nodejs/node/pull/6093
    description: Added support for `SIGINFO`.
-->

The following signal constants are exported by `os.constants.signals`.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>SIGHUP</code></td>
    <td>Sent to indicate when a controlling terminal is closed or a parent
    process exits.</td>
  </tr>
  <tr>
    <td><code>SIGINT</code></td>
    <td>Sent to indicate when a user wishes to interrupt a process
    (<kbd>Ctrl</kbd>+<kbd>C</kbd>).</td>
  </tr>
  <tr>
    <td><code>SIGQUIT</code></td>
    <td>Sent to indicate when a user wishes to terminate a process and perform a
    core dump.</td>
  </tr>
  <tr>
    <td><code>SIGILL</code></td>
    <td>Sent to a process to notify that it has attempted to perform an illegal,
    malformed, unknown, or privileged instruction.</td>
  </tr>
  <tr>
    <td><code>SIGTRAP</code></td>
    <td>Sent to a process when an exception has occurred.</td>
  </tr>
  <tr>
    <td><code>SIGABRT</code></td>
    <td>Sent to a process to request that it abort.</td>
  </tr>
  <tr>
    <td><code>SIGIOT</code></td>
    <td>Synonym for <code>SIGABRT</code></td>
  </tr>
  <tr>
    <td><code>SIGBUS</code></td>
    <td>Sent to a process to notify that it has caused a bus error.</td>
  </tr>
  <tr>
    <td><code>SIGFPE</code></td>
    <td>Sent to a process to notify that it has performed an illegal arithmetic
    operation.</td>
  </tr>
  <tr>
    <td><code>SIGKILL</code></td>
    <td>Sent to a process to terminate it immediately.</td>
  </tr>
  <tr>
    <td><code>SIGUSR1</code> <code>SIGUSR2</code></td>
    <td>Sent to a process to identify user-defined conditions.</td>
  </tr>
  <tr>
    <td><code>SIGSEGV</code></td>
    <td>Sent to a process to notify of a segmentation fault.</td>
  </tr>
  <tr>
    <td><code>SIGPIPE</code></td>
    <td>Sent to a process when it has attempted to write to a disconnected
    pipe.</td>
  </tr>
  <tr>
    <td><code>SIGALRM</code></td>
    <td>Sent to a process when a system timer elapses.</td>
  </tr>
  <tr>
    <td><code>SIGTERM</code></td>
    <td>Sent to a process to request termination.</td>
  </tr>
  <tr>
    <td><code>SIGCHLD</code></td>
    <td>Sent to a process when a child process terminates.</td>
  </tr>
  <tr>
    <td><code>SIGSTKFLT</code></td>
    <td>Sent to a process to indicate a stack fault on a coprocessor.</td>
  </tr>
  <tr>
    <td><code>SIGCONT</code></td>
    <td>Sent to instruct the operating system to continue a paused process.</td>
  </tr>
  <tr>
    <td><code>SIGSTOP</code></td>
    <td>Sent to instruct the operating system to halt a process.</td>
  </tr>
  <tr>
    <td><code>SIGTSTP</code></td>
    <td>Sent to a process to request it to stop.</td>
  </tr>
  <tr>
    <td><code>SIGBREAK</code></td>
    <td>Sent to indicate when a user wishes to interrupt a process.</td>
  </tr>
  <tr>
    <td><code>SIGTTIN</code></td>
    <td>Sent to a process when it reads from the TTY while in the
    background.</td>
  </tr>
  <tr>
    <td><code>SIGTTOU</code></td>
    <td>Sent to a process when it writes to the TTY while in the
    background.</td>
  </tr>
  <tr>
    <td><code>SIGURG</code></td>
    <td>Sent to a process when a socket has urgent data to read.</td>
  </tr>
  <tr>
    <td><code>SIGXCPU</code></td>
    <td>Sent to a process when it has exceeded its limit on CPU usage.</td>
  </tr>
  <tr>
    <td><code>SIGXFSZ</code></td>
    <td>Sent to a process when it grows a file larger than the maximum
    allowed.</td>
  </tr>
  <tr>
    <td><code>SIGVTALRM</code></td>
    <td>Sent to a process when a virtual timer has elapsed.</td>
  </tr>
  <tr>
    <td><code>SIGPROF</code></td>
    <td>Sent to a process when a system timer has elapsed.</td>
  </tr>
  <tr>
    <td><code>SIGWINCH</code></td>
    <td>Sent to a process when the controlling terminal has changed its
    size.</td>
  </tr>
  <tr>
    <td><code>SIGIO</code></td>
    <td>Sent to a process when I/O is available.</td>
  </tr>
  <tr>
    <td><code>SIGPOLL</code></td>
    <td>Synonym for <code>SIGIO</code></td>
  </tr>
  <tr>
    <td><code>SIGLOST</code></td>
    <td>Sent to a process when a file lock has been lost.</td>
  </tr>
  <tr>
    <td><code>SIGPWR</code></td>
    <td>Sent to a process to notify of a power failure.</td>
  </tr>
  <tr>
    <td><code>SIGINFO</code></td>
    <td>Synonym for <code>SIGPWR</code></td>
  </tr>
  <tr>
    <td><code>SIGSYS</code></td>
    <td>Sent to a process to notify of a bad argument.</td>
  </tr>
  <tr>
    <td><code>SIGUNUSED</code></td>
    <td>Synonym for <code>SIGSYS</code></td>
  </tr>
</table>

### Error constants

The following error constants are exported by `os.constants.errno`.

#### POSIX error constants

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>E2BIG</code></td>
    <td>Indicates that the list of arguments is longer than expected.</td>
  </tr>
  <tr>
    <td><code>EACCES</code></td>
    <td>Indicates that the operation did not have sufficient permissions.</td>
  </tr>
  <tr>
    <td><code>EADDRINUSE</code></td>
    <td>Indicates that the network address is already in use.</td>
  </tr>
  <tr>
    <td><code>EADDRNOTAVAIL</code></td>
    <td>Indicates that the network address is currently unavailable for
    use.</td>
  </tr>
  <tr>
    <td><code>EAFNOSUPPORT</code></td>
    <td>Indicates that the network address family is not supported.</td>
  </tr>
  <tr>
    <td><code>EAGAIN</code></td>
    <td>Indicates that there is no data available and to try the
    operation again later.</td>
  </tr>
  <tr>
    <td><code>EALREADY</code></td>
    <td>Indicates that the socket already has a pending connection in
    progress.</td>
  </tr>
  <tr>
    <td><code>EBADF</code></td>
    <td>Indicates that a file descriptor is not valid.</td>
  </tr>
  <tr>
    <td><code>EBADMSG</code></td>
    <td>Indicates an invalid data message.</td>
  </tr>
  <tr>
    <td><code>EBUSY</code></td>
    <td>Indicates that a device or resource is busy.</td>
  </tr>
  <tr>
    <td><code>ECANCELED</code></td>
    <td>Indicates that an operation was canceled.</td>
  </tr>
  <tr>
    <td><code>ECHILD</code></td>
    <td>Indicates that there are no child processes.</td>
  </tr>
  <tr>
    <td><code>ECONNABORTED</code></td>
    <td>Indicates that the network connection has been aborted.</td>
  </tr>
  <tr>
    <td><code>ECONNREFUSED</code></td>
    <td>Indicates that the network connection has been refused.</td>
  </tr>
  <tr>
    <td><code>ECONNRESET</code></td>
    <td>Indicates that the network connection has been reset.</td>
  </tr>
  <tr>
    <td><code>EDEADLK</code></td>
    <td>Indicates that a resource deadlock has been avoided.</td>
  </tr>
  <tr>
    <td><code>EDESTADDRREQ</code></td>
    <td>Indicates that a destination address is required.</td>
  </tr>
  <tr>
    <td><code>EDOM</code></td>
    <td>Indicates that an argument is out of the domain of the function.</td>
  </tr>
  <tr>
    <td><code>EDQUOT</code></td>
    <td>Indicates that the disk quota has been exceeded.</td>
  </tr>
  <tr>
    <td><code>EEXIST</code></td>
    <td>Indicates that the file already exists.</td>
  </tr>
  <tr>
    <td><code>EFAULT</code></td>
    <td>Indicates an invalid pointer address.</td>
  </tr>
  <tr>
    <td><code>EFBIG</code></td>
    <td>Indicates that the file is too large.</td>
  </tr>
  <tr>
    <td><code>EHOSTUNREACH</code></td>
    <td>Indicates that the host is unreachable.</td>
  </tr>
  <tr>
    <td><code>EIDRM</code></td>
    <td>Indicates that the identifier has been removed.</td>
  </tr>
  <tr>
    <td><code>EILSEQ</code></td>
    <td>Indicates an illegal byte sequence.</td>
  </tr>
  <tr>
    <td><code>EINPROGRESS</code></td>
    <td>Indicates that an operation is already in progress.</td>
  </tr>
  <tr>
    <td><code>EINTR</code></td>
    <td>Indicates that a function call was interrupted.</td>
  </tr>
  <tr>
    <td><code>EINVAL</code></td>
    <td>Indicates that an invalid argument was provided.</td>
  </tr>
  <tr>
    <td><code>EIO</code></td>
    <td>Indicates an otherwise unspecified I/O error.</td>
  </tr>
  <tr>
    <td><code>EISCONN</code></td>
    <td>Indicates that the socket is connected.</td>
  </tr>
  <tr>
    <td><code>EISDIR</code></td>
    <td>Indicates that the path is a directory.</td>
  </tr>
  <tr>
    <td><code>ELOOP</code></td>
    <td>Indicates too many levels of symbolic links in a path.</td>
  </tr>
  <tr>
    <td><code>EMFILE</code></td>
    <td>Indicates that there are too many open files.</td>
  </tr>
  <tr>
    <td><code>EMLINK</code></td>
    <td>Indicates that there are too many hard links to a file.</td>
  </tr>
  <tr>
    <td><code>EMSGSIZE</code></td>
    <td>Indicates that the provided message is too long.</td>
  </tr>
  <tr>
    <td><code>EMULTIHOP</code></td>
    <td>Indicates that a multihop was attempted.</td>
  </tr>
  <tr>
    <td><code>ENAMETOOLONG</code></td>
    <td>Indicates that the filename is too long.</td>
  </tr>
  <tr>
    <td><code>ENETDOWN</code></td>
    <td>Indicates that the network is down.</td>
  </tr>
  <tr>
    <td><code>ENETRESET</code></td>
    <td>Indicates that the connection has been aborted by the network.</td>
  </tr>
  <tr>
    <td><code>ENETUNREACH</code></td>
    <td>Indicates that the network is unreachable.</td>
  </tr>
  <tr>
    <td><code>ENFILE</code></td>
    <td>Indicates too many open files in the system.</td>
  </tr>
  <tr>
    <td><code>ENOBUFS</code></td>
    <td>Indicates that no buffer space is available.</td>
  </tr>
  <tr>
    <td><code>ENODATA</code></td>
    <td>Indicates that no message is available on the stream head read
    queue.</td>
  </tr>
  <tr>
    <td><code>ENODEV</code></td>
    <td>Indicates that there is no such device.</td>
  </tr>
  <tr>
    <td><code>ENOENT</code></td>
    <td>Indicates that there is no such file or directory.</td>
  </tr>
  <tr>
    <td><code>ENOEXEC</code></td>
    <td>Indicates an exec format error.</td>
  </tr>
  <tr>
    <td><code>ENOLCK</code></td>
    <td>Indicates that there are no locks available.</td>
  </tr>
  <tr>
    <td><code>ENOLINK</code></td>
    <td>Indications that a link has been severed.</td>
  </tr>
  <tr>
    <td><code>ENOMEM</code></td>
    <td>Indicates that there is not enough space.</td>
  </tr>
  <tr>
    <td><code>ENOMSG</code></td>
    <td>Indicates that there is no message of the desired type.</td>
  </tr>
  <tr>
    <td><code>ENOPROTOOPT</code></td>
    <td>Indicates that a given protocol is not available.</td>
  </tr>
  <tr>
    <td><code>ENOSPC</code></td>
    <td>Indicates that there is no space available on the device.</td>
  </tr>
  <tr>
    <td><code>ENOSR</code></td>
    <td>Indicates that there are no stream resources available.</td>
  </tr>
  <tr>
    <td><code>ENOSTR</code></td>
    <td>Indicates that a given resource is not a stream.</td>
  </tr>
  <tr>
    <td><code>ENOSYS</code></td>
    <td>Indicates that a function has not been implemented.</td>
  </tr>
  <tr>
    <td><code>ENOTCONN</code></td>
    <td>Indicates that the socket is not connected.</td>
  </tr>
  <tr>
    <td><code>ENOTDIR</code></td>
    <td>Indicates that the path is not a directory.</td>
  </tr>
  <tr>
    <td><code>ENOTEMPTY</code></td>
    <td>Indicates that the directory is not empty.</td>
  </tr>
  <tr>
    <td><code>ENOTSOCK</code></td>
    <td>Indicates that the given item is not a socket.</td>
  </tr>
  <tr>
    <td><code>ENOTSUP</code></td>
    <td>Indicates that a given operation is not supported.</td>
  </tr>
  <tr>
    <td><code>ENOTTY</code></td>
    <td>Indicates an inappropriate I/O control operation.</td>
  </tr>
  <tr>
    <td><code>ENXIO</code></td>
    <td>Indicates no such device or address.</td>
  </tr>
  <tr>
    <td><code>EOPNOTSUPP</code></td>
    <td>Indicates that an operation is not supported on the socket. Although
    <code>ENOTSUP</code> and <code>EOPNOTSUPP</code> have the same value
    on Linux, according to POSIX.1 these error values should be distinct.)</td>
  </tr>
  <tr>
    <td><code>EOVERFLOW</code></td>
    <td>Indicates that a value is too large to be stored in a given data
    type.</td>
  </tr>
  <tr>
    <td><code>EPERM</code></td>
    <td>Indicates that the operation is not permitted.</td>
  </tr>
  <tr>
    <td><code>EPIPE</code></td>
    <td>Indicates a broken pipe.</td>
  </tr>
  <tr>
    <td><code>EPROTO</code></td>
    <td>Indicates a protocol error.</td>
  </tr>
  <tr>
    <td><code>EPROTONOSUPPORT</code></td>
    <td>Indicates that a protocol is not supported.</td>
  </tr>
  <tr>
    <td><code>EPROTOTYPE</code></td>
    <td>Indicates the wrong type of protocol for a socket.</td>
  </tr>
  <tr>
    <td><code>ERANGE</code></td>
    <td>Indicates that the results are too large.</td>
  </tr>
  <tr>
    <td><code>EROFS</code></td>
    <td>Indicates that the file system is read only.</td>
  </tr>
  <tr>
    <td><code>ESPIPE</code></td>
    <td>Indicates an invalid seek operation.</td>
  </tr>
  <tr>
    <td><code>ESRCH</code></td>
    <td>Indicates that there is no such process.</td>
  </tr>
  <tr>
    <td><code>ESTALE</code></td>
    <td>Indicates that the file handle is stale.</td>
  </tr>
  <tr>
    <td><code>ETIME</code></td>
    <td>Indicates an expired timer.</td>
  </tr>
  <tr>
    <td><code>ETIMEDOUT</code></td>
    <td>Indicates that the connection timed out.</td>
  </tr>
  <tr>
    <td><code>ETXTBSY</code></td>
    <td>Indicates that a text file is busy.</td>
  </tr>
  <tr>
    <td><code>EWOULDBLOCK</code></td>
    <td>Indicates that the operation would block.</td>
  </tr>
  <tr>
    <td><code>EXDEV</code></td>
    <td>Indicates an improper link.</td>
  </tr>
</table>

#### Windows-specific error constants

The following error codes are specific to the Windows operating system.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>WSAEINTR</code></td>
    <td>Indicates an interrupted function call.</td>
  </tr>
  <tr>
    <td><code>WSAEBADF</code></td>
    <td>Indicates an invalid file handle.</td>
  </tr>
  <tr>
    <td><code>WSAEACCES</code></td>
    <td>Indicates insufficient permissions to complete the operation.</td>
  </tr>
  <tr>
    <td><code>WSAEFAULT</code></td>
    <td>Indicates an invalid pointer address.</td>
  </tr>
  <tr>
    <td><code>WSAEINVAL</code></td>
    <td>Indicates that an invalid argument was passed.</td>
  </tr>
  <tr>
    <td><code>WSAEMFILE</code></td>
    <td>Indicates that there are too many open files.</td>
  </tr>
  <tr>
    <td><code>WSAEWOULDBLOCK</code></td>
    <td>Indicates that a resource is temporarily unavailable.</td>
  </tr>
  <tr>
    <td><code>WSAEINPROGRESS</code></td>
    <td>Indicates that an operation is currently in progress.</td>
  </tr>
  <tr>
    <td><code>WSAEALREADY</code></td>
    <td>Indicates that an operation is already in progress.</td>
  </tr>
  <tr>
    <td><code>WSAENOTSOCK</code></td>
    <td>Indicates that the resource is not a socket.</td>
  </tr>
  <tr>
    <td><code>WSAEDESTADDRREQ</code></td>
    <td>Indicates that a destination address is required.</td>
  </tr>
  <tr>
    <td><code>WSAEMSGSIZE</code></td>
    <td>Indicates that the message size is too long.</td>
  </tr>
  <tr>
    <td><code>WSAEPROTOTYPE</code></td>
    <td>Indicates the wrong protocol type for the socket.</td>
  </tr>
  <tr>
    <td><code>WSAENOPROTOOPT</code></td>
    <td>Indicates a bad protocol option.</td>
  </tr>
  <tr>
    <td><code>WSAEPROTONOSUPPORT</code></td>
    <td>Indicates that the protocol is not supported.</td>
  </tr>
  <tr>
    <td><code>WSAESOCKTNOSUPPORT</code></td>
    <td>Indicates that the socket type is not supported.</td>
  </tr>
  <tr>
    <td><code>WSAEOPNOTSUPP</code></td>
    <td>Indicates that the operation is not supported.</td>
  </tr>
  <tr>
    <td><code>WSAEPFNOSUPPORT</code></td>
    <td>Indicates that the protocol family is not supported.</td>
  </tr>
  <tr>
    <td><code>WSAEAFNOSUPPORT</code></td>
    <td>Indicates that the address family is not supported.</td>
  </tr>
  <tr>
    <td><code>WSAEADDRINUSE</code></td>
    <td>Indicates that the network address is already in use.</td>
  </tr>
  <tr>
    <td><code>WSAEADDRNOTAVAIL</code></td>
    <td>Indicates that the network address is not available.</td>
  </tr>
  <tr>
    <td><code>WSAENETDOWN</code></td>
    <td>Indicates that the network is down.</td>
  </tr>
  <tr>
    <td><code>WSAENETUNREACH</code></td>
    <td>Indicates that the network is unreachable.</td>
  </tr>
  <tr>
    <td><code>WSAENETRESET</code></td>
    <td>Indicates that the network connection has been reset.</td>
  </tr>
  <tr>
    <td><code>WSAECONNABORTED</code></td>
    <td>Indicates that the connection has been aborted.</td>
  </tr>
  <tr>
    <td><code>WSAECONNRESET</code></td>
    <td>Indicates that the connection has been reset by the peer.</td>
  </tr>
  <tr>
    <td><code>WSAENOBUFS</code></td>
    <td>Indicates that there is no buffer space available.</td>
  </tr>
  <tr>
    <td><code>WSAEISCONN</code></td>
    <td>Indicates that the socket is already connected.</td>
  </tr>
  <tr>
    <td><code>WSAENOTCONN</code></td>
    <td>Indicates that the socket is not connected.</td>
  </tr>
  <tr>
    <td><code>WSAESHUTDOWN</code></td>
    <td>Indicates that data cannot be sent after the socket has been
    shutdown.</td>
  </tr>
  <tr>
    <td><code>WSAETOOMANYREFS</code></td>
    <td>Indicates that there are too many references.</td>
  </tr>
  <tr>
    <td><code>WSAETIMEDOUT</code></td>
    <td>Indicates that the connection has timed out.</td>
  </tr>
  <tr>
    <td><code>WSAECONNREFUSED</code></td>
    <td>Indicates that the connection has been refused.</td>
  </tr>
  <tr>
    <td><code>WSAELOOP</code></td>
    <td>Indicates that a name cannot be translated.</td>
  </tr>
  <tr>
    <td><code>WSAENAMETOOLONG</code></td>
    <td>Indicates that a name was too long.</td>
  </tr>
  <tr>
    <td><code>WSAEHOSTDOWN</code></td>
    <td>Indicates that a network host is down.</td>
  </tr>
  <tr>
    <td><code>WSAEHOSTUNREACH</code></td>
    <td>Indicates that there is no route to a network host.</td>
  </tr>
  <tr>
    <td><code>WSAENOTEMPTY</code></td>
    <td>Indicates that the directory is not empty.</td>
  </tr>
  <tr>
    <td><code>WSAEPROCLIM</code></td>
    <td>Indicates that there are too many processes.</td>
  </tr>
  <tr>
    <td><code>WSAEUSERS</code></td>
    <td>Indicates that the user quota has been exceeded.</td>
  </tr>
  <tr>
    <td><code>WSAEDQUOT</code></td>
    <td>Indicates that the disk quota has been exceeded.</td>
  </tr>
  <tr>
    <td><code>WSAESTALE</code></td>
    <td>Indicates a stale file handle reference.</td>
  </tr>
  <tr>
    <td><code>WSAEREMOTE</code></td>
    <td>Indicates that the item is remote.</td>
  </tr>
  <tr>
    <td><code>WSASYSNOTREADY</code></td>
    <td>Indicates that the network subsystem is not ready.</td>
  </tr>
  <tr>
    <td><code>WSAVERNOTSUPPORTED</code></td>
    <td>Indicates that the <code>winsock.dll</code> version is out of
    range.</td>
  </tr>
  <tr>
    <td><code>WSANOTINITIALISED</code></td>
    <td>Indicates that successful WSAStartup has not yet been performed.</td>
  </tr>
  <tr>
    <td><code>WSAEDISCON</code></td>
    <td>Indicates that a graceful shutdown is in progress.</td>
  </tr>
  <tr>
    <td><code>WSAENOMORE</code></td>
    <td>Indicates that there are no more results.</td>
  </tr>
  <tr>
    <td><code>WSAECANCELLED</code></td>
    <td>Indicates that an operation has been canceled.</td>
  </tr>
  <tr>
    <td><code>WSAEINVALIDPROCTABLE</code></td>
    <td>Indicates that the procedure call table is invalid.</td>
  </tr>
  <tr>
    <td><code>WSAEINVALIDPROVIDER</code></td>
    <td>Indicates an invalid service provider.</td>
  </tr>
  <tr>
    <td><code>WSAEPROVIDERFAILEDINIT</code></td>
    <td>Indicates that the service provider failed to initialized.</td>
  </tr>
  <tr>
    <td><code>WSASYSCALLFAILURE</code></td>
    <td>Indicates a system call failure.</td>
  </tr>
  <tr>
    <td><code>WSASERVICE_NOT_FOUND</code></td>
    <td>Indicates that a service was not found.</td>
  </tr>
  <tr>
    <td><code>WSATYPE_NOT_FOUND</code></td>
    <td>Indicates that a class type was not found.</td>
  </tr>
  <tr>
    <td><code>WSA_E_NO_MORE</code></td>
    <td>Indicates that there are no more results.</td>
  </tr>
  <tr>
    <td><code>WSA_E_CANCELLED</code></td>
    <td>Indicates that the call was canceled.</td>
  </tr>
  <tr>
    <td><code>WSAEREFUSED</code></td>
    <td>Indicates that a database query was refused.</td>
  </tr>
</table>

### dlopen constants

If available on the operating system, the following constants
are exported in `os.constants.dlopen`. See dlopen(3) for detailed
information.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>RTLD_LAZY</code></td>
    <td>Perform lazy binding. Node.js sets this flag by default.</td>
  </tr>
  <tr>
    <td><code>RTLD_NOW</code></td>
    <td>Resolve all undefined symbols in the library before dlopen(3)
    returns.</td>
  </tr>
  <tr>
    <td><code>RTLD_GLOBAL</code></td>
    <td>Symbols defined by the library will be made available for symbol
    resolution of subsequently loaded libraries.</td>
  </tr>
  <tr>
    <td><code>RTLD_LOCAL</code></td>
    <td>The converse of <code>RTLD_GLOBAL</code>. This is the default behavior
    if neither flag is specified.</td>
  </tr>
  <tr>
    <td><code>RTLD_DEEPBIND</code></td>
    <td>Make a self-contained library use its own symbols in preference to
    symbols from previously loaded libraries.</td>
  </tr>
</table>

### Priority constants

<!-- YAML
added: v10.10.0
-->

The following process scheduling constants are exported by
`os.constants.priority`.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>PRIORITY_LOW</code></td>
    <td>The lowest process scheduling priority. This corresponds to
    <code>IDLE_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>19</code> on all other platforms.</td>
  </tr>
  <tr>
    <td><code>PRIORITY_BELOW_NORMAL</code></td>
    <td>The process scheduling priority above <code>PRIORITY_LOW</code> and
    below <code>PRIORITY_NORMAL</code>. This corresponds to
    <code>BELOW_NORMAL_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>10</code> on all other platforms.</td>
  </tr>
  <tr>
    <td><code>PRIORITY_NORMAL</code></td>
    <td>The default process scheduling priority. This corresponds to
    <code>NORMAL_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>0</code> on all other platforms.</td>
  </tr>
  <tr>
    <td><code>PRIORITY_ABOVE_NORMAL</code></td>
    <td>The process scheduling priority above <code>PRIORITY_NORMAL</code> and
    below <code>PRIORITY_HIGH</code>. This corresponds to
    <code>ABOVE_NORMAL_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>-7</code> on all other platforms.</td>
  </tr>
  <tr>
    <td><code>PRIORITY_HIGH</code></td>
    <td>The process scheduling priority above <code>PRIORITY_ABOVE_NORMAL</code>
    and below <code>PRIORITY_HIGHEST</code>. This corresponds to
    <code>HIGH_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>-14</code> on all other platforms.</td>
  </tr>
  <tr>
    <td><code>PRIORITY_HIGHEST</code></td>
    <td>The highest process scheduling priority. This corresponds to
    <code>REALTIME_PRIORITY_CLASS</code> on Windows, and a nice value of
    <code>-20</code> on all other platforms.</td>
  </tr>
</table>

### libuv constants

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>UV_UDP_REUSEADDR</code></td>
    <td></td>
  </tr>
</table>

[Android building]: https://github.com/nodejs/node/blob/HEAD/BUILDING.md#android
[EUID]: https://en.wikipedia.org/wiki/User_identifier#Effective_user_ID
[`SystemError`]: errors.md#class-systemerror
[`process.arch`]: process.md#processarch
[`process.platform`]: process.md#processplatform
[`uname(3)`]: https://linux.die.net/man/3/uname
[`uv_available_parallelism()`]: https://docs.libuv.org/en/v1.x/misc.html#c.uv_available_parallelism
