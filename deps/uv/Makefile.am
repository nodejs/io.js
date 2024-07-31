# Copyright (c) 2013, Ben Noordhuis <info@bnoordhuis.nl>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(top_srcdir)/include \
              -I$(top_srcdir)/src

include_HEADERS=include/uv.h

uvincludedir = $(includedir)/uv
uvinclude_HEADERS = include/uv/errno.h \
                    include/uv/threadpool.h \
                    include/uv/version.h

CLEANFILES =

lib_LTLIBRARIES = libuv.la
libuv_la_CFLAGS = $(AM_CFLAGS)
libuv_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined -version-info 1:0:0
libuv_la_SOURCES = src/fs-poll.c \
                   src/heap-inl.h \
                   src/idna.c \
                   src/idna.h \
                   src/inet.c \
                   src/queue.h \
                   src/random.c \
                   src/strscpy.c \
                   src/strscpy.h \
                   src/thread-common.c \
                   src/threadpool.c \
                   src/timer.c \
                   src/uv-data-getter-setters.c \
                   src/uv-common.c \
                   src/uv-common.h \
                   src/version.c \
                   src/strtok.c \
                   src/strtok.h

if SUNOS
# Can't be turned into a CC_CHECK_CFLAGS in configure.ac, it makes compilers
# on other platforms complain that the argument is unused during compilation.
libuv_la_CFLAGS += -pthreads
endif

if WINNT

uvinclude_HEADERS += include/uv/win.h include/uv/tree.h
AM_CPPFLAGS += -I$(top_srcdir)/src/win \
               -DWIN32_LEAN_AND_MEAN \
               -D_WIN32_WINNT=0x0602
libuv_la_SOURCES += src/win/async.c \
                    src/win/atomicops-inl.h \
                    src/win/core.c \
                    src/win/detect-wakeup.c \
                    src/win/dl.c \
                    src/win/error.c \
                    src/win/fs-event.c \
                    src/win/fs.c \
                    src/win/getaddrinfo.c \
                    src/win/getnameinfo.c \
                    src/win/handle.c \
                    src/win/handle-inl.h \
                    src/win/internal.h \
                    src/win/loop-watcher.c \
                    src/win/pipe.c \
                    src/win/poll.c \
                    src/win/process-stdio.c \
                    src/win/process.c \
                    src/win/req-inl.h \
                    src/win/signal.c \
                    src/win/stream.c \
                    src/win/stream-inl.h \
                    src/win/tcp.c \
                    src/win/thread.c \
                    src/win/tty.c \
                    src/win/udp.c \
                    src/win/util.c \
                    src/win/winapi.c \
                    src/win/winapi.h \
                    src/win/winsock.c \
                    src/win/winsock.h

else  # WINNT

uvinclude_HEADERS += include/uv/unix.h
AM_CPPFLAGS += -I$(top_srcdir)/src/unix
libuv_la_SOURCES += src/unix/async.c \
                   src/unix/core.c \
                   src/unix/dl.c \
                   src/unix/fs.c \
                   src/unix/getaddrinfo.c \
                   src/unix/getnameinfo.c \
                   src/unix/internal.h \
                   src/unix/loop-watcher.c \
                   src/unix/loop.c \
                   src/unix/pipe.c \
                   src/unix/poll.c \
                   src/unix/process.c \
                   src/unix/random-devurandom.c \
                   src/unix/signal.c \
                   src/unix/stream.c \
                   src/unix/tcp.c \
                   src/unix/thread.c \
                   src/unix/tty.c \
                   src/unix/udp.c

endif  # WINNT

EXTRA_DIST = test/fixtures/empty_file \
             test/fixtures/load_error.node \
             test/fixtures/lorem_ipsum.txt \
             test/fixtures/one_file/one_file \
             include \
             docs \
             img \
             CONTRIBUTING.md \
             LICENSE \
             LICENSE-extra \
             README.md



TESTS = test/run-tests
check_PROGRAMS = test/run-tests
test_run_tests_CFLAGS = $(AM_CFLAGS)

if WINNT
check-am: test/run-tests_no_ext
test/run-tests_no_ext: test/run-tests$(EXEEXT)
	cp test/run-tests$(EXEEXT) test/run-tests_no_ext
endif

if SUNOS
# Can't be turned into a CC_CHECK_CFLAGS in configure.ac, it makes compilers
# on other platforms complain that the argument is unused during compilation.
test_run_tests_CFLAGS += -pthreads
endif

test_run_tests_LDFLAGS = $(AM_LDFLAGS)
test_run_tests_SOURCES = test/blackhole-server.c \
                         test/echo-server.c \
                         test/run-tests.c \
                         test/runner.c \
                         test/runner.h \
                         test/task.h \
                         test/test-active.c \
                         test/test-async.c \
                         test/test-async-null-cb.c \
                         test/test-barrier.c \
                         test/test-callback-stack.c \
                         test/test-close-fd.c \
                         test/test-close-order.c \
                         test/test-condvar.c \
                         test/test-connect-unspecified.c \
                         test/test-connection-fail.c \
                         test/test-cwd-and-chdir.c \
                         test/test-default-loop-close.c \
                         test/test-delayed-accept.c \
                         test/test-dlerror.c \
                         test/test-eintr-handling.c \
                         test/test-embed.c \
                         test/test-emfile.c \
                         test/test-env-vars.c \
                         test/test-error.c \
                         test/test-fail-always.c \
                         test/test-fs-copyfile.c \
                         test/test-fs-event.c \
                         test/test-fs-poll.c \
                         test/test-fs.c \
                         test/test-fs-readdir.c \
                         test/test-fs-fd-hash.c \
                         test/test-fs-open-flags.c \
                         test/test-fork.c \
                         test/test-getters-setters.c \
                         test/test-get-currentexe.c \
                         test/test-get-loadavg.c \
                         test/test-get-memory.c \
                         test/test-get-passwd.c \
                         test/test-getaddrinfo.c \
                         test/test-gethostname.c \
                         test/test-getnameinfo.c \
                         test/test-getsockname.c \
                         test/test-gettimeofday.c \
                         test/test-handle-fileno.c \
                         test/test-homedir.c \
                         test/test-hrtime.c \
                         test/test-idle.c \
                         test/test-idna.c \
                         test/test-ip4-addr.c \
                         test/test-ip6-addr.c \
                         test/test-ip-name.c \
                         test/test-ipc-heavy-traffic-deadlock-bug.c \
                         test/test-ipc-send-recv.c \
                         test/test-ipc.c \
                         test/test-list.h \
                         test/test-loop-handles.c \
                         test/test-loop-alive.c \
                         test/test-loop-close.c \
                         test/test-loop-stop.c \
                         test/test-loop-time.c \
                         test/test-loop-configure.c \
                         test/test-metrics.c \
                         test/test-multiple-listen.c \
                         test/test-mutexes.c \
                         test/test-not-readable-nor-writable-on-read-error.c \
                         test/test-not-writable-after-shutdown.c \
                         test/test-osx-select.c \
                         test/test-pass-always.c \
                         test/test-ping-pong.c \
                         test/test-pipe-bind-error.c \
                         test/test-pipe-connect-error.c \
                         test/test-pipe-connect-multiple.c \
                         test/test-pipe-connect-prepare.c \
                         test/test-pipe-getsockname.c \
                         test/test-pipe-pending-instances.c \
                         test/test-pipe-sendmsg.c \
                         test/test-pipe-server-close.c \
                         test/test-pipe-close-stdout-read-stdin.c \
                         test/test-pipe-set-non-blocking.c \
                         test/test-pipe-set-fchmod.c \
                         test/test-platform-output.c \
                         test/test-poll.c \
                         test/test-poll-close.c \
                         test/test-poll-close-doesnt-corrupt-stack.c \
                         test/test-poll-closesocket.c \
                         test/test-poll-multiple-handles.c \
                         test/test-poll-oob.c \
                         test/test-process-priority.c \
                         test/test-process-title.c \
                         test/test-process-title-threadsafe.c \
                         test/test-queue-foreach-delete.c \
                         test/test-random.c \
                         test/test-readable-on-eof.c \
                         test/test-ref.c \
                         test/test-run-nowait.c \
                         test/test-run-once.c \
                         test/test-semaphore.c \
                         test/test-shutdown-close.c \
                         test/test-shutdown-eof.c \
                         test/test-shutdown-simultaneous.c \
                         test/test-shutdown-twice.c \
                         test/test-signal-multiple-loops.c \
                         test/test-signal-pending-on-close.c \
                         test/test-signal.c \
                         test/test-socket-buffer-size.c \
                         test/test-spawn.c \
                         test/test-stdio-over-pipes.c \
                         test/test-strscpy.c \
                         test/test-strtok.c \
                         test/test-tcp-alloc-cb-fail.c \
                         test/test-tcp-bind-error.c \
                         test/test-tcp-bind6-error.c \
                         test/test-tcp-close-accept.c \
                         test/test-tcp-close-while-connecting.c \
                         test/test-tcp-close-after-read-timeout.c \
                         test/test-tcp-close.c \
                         test/test-tcp-close-reset.c \
                         test/test-tcp-create-socket-early.c \
                         test/test-tcp-connect-error-after-write.c \
                         test/test-tcp-connect-error.c \
                         test/test-tcp-connect-timeout.c \
                         test/test-tcp-connect6-error.c \
                         test/test-tcp-flags.c \
                         test/test-tcp-open.c \
                         test/test-tcp-read-stop.c \
                         test/test-tcp-read-stop-start.c \
                         test/test-tcp-rst.c \
                         test/test-tcp-shutdown-after-write.c \
                         test/test-tcp-unexpected-read.c \
                         test/test-tcp-oob.c \
                         test/test-tcp-write-to-half-open-connection.c \
                         test/test-tcp-write-after-connect.c \
                         test/test-tcp-writealot.c \
                         test/test-tcp-write-fail.c \
                         test/test-tcp-try-write.c \
                         test/test-tcp-write-in-a-row.c \
                         test/test-tcp-try-write-error.c \
                         test/test-tcp-write-queue-order.c \
                         test/test-test-macros.c \
                         test/test-thread-equal.c \
                         test/test-thread.c \
                         test/test-thread-affinity.c \
                         test/test-thread-priority.c \
                         test/test-threadpool-cancel.c \
                         test/test-threadpool.c \
                         test/test-timer-again.c \
                         test/test-timer-from-check.c \
                         test/test-timer.c \
                         test/test-tmpdir.c \
                         test/test-tty-duplicate-key.c \
                         test/test-tty-escape-sequence-processing.c \
                         test/test-tty.c \
                         test/test-udp-alloc-cb-fail.c \
                         test/test-udp-bind.c \
                         test/test-udp-connect.c \
                         test/test-udp-connect6.c \
                         test/test-udp-create-socket-early.c \
                         test/test-udp-dgram-too-big.c \
                         test/test-udp-ipv6.c \
                         test/test-udp-mmsg.c \
                         test/test-udp-multicast-interface.c \
                         test/test-udp-multicast-interface6.c \
                         test/test-udp-multicast-join.c \
                         test/test-udp-multicast-join6.c \
                         test/test-udp-multicast-ttl.c \
                         test/test-udp-open.c \
                         test/test-udp-options.c \
                         test/test-udp-send-and-recv.c \
                         test/test-udp-send-hang-loop.c \
                         test/test-udp-send-immediate.c \
                         test/test-udp-sendmmsg-error.c \
                         test/test-udp-send-unreachable.c \
                         test/test-udp-try-send.c \
                         test/test-udp-recv-in-a-row.c \
                         test/test-uname.c \
                         test/test-walk-handles.c \
                         test/test-watcher-cross-stop.c
test_run_tests_LDADD = libuv.la

if WINNT
test_run_tests_SOURCES += test/runner-win.c \
                          test/runner-win.h
else
test_run_tests_SOURCES += test/runner-unix.c \
                          test/runner-unix.h
endif

if AIX
test_run_tests_CFLAGS += -D_ALL_SOURCE \
                         -D_XOPEN_SOURCE=500 \
                         -D_LINUX_SOURCE_COMPAT
endif

if OS400
test_run_tests_CFLAGS += -D_ALL_SOURCE \
                         -D_XOPEN_SOURCE=500 \
                         -D_LINUX_SOURCE_COMPAT
endif

if HAIKU
test_run_tests_CFLAGS += -D_BSD_SOURCE
endif

if LINUX
test_run_tests_CFLAGS += -D_GNU_SOURCE
endif

if SUNOS
test_run_tests_CFLAGS += -D__EXTENSIONS__ \
                         -D_XOPEN_SOURCE=500 \
                         -D_REENTRANT
endif

if OS390
test_run_tests_CFLAGS += -D_ISOC99_SOURCE \
                         -D_UNIX03_THREADS \
                         -D_UNIX03_SOURCE \
                         -D_OPEN_SYS_IF_EXT=1 \
                         -D_OPEN_SYS_SOCK_IPV6 \
                         -D_OPEN_MSGQ_EXT \
                         -D_XOPEN_SOURCE_EXTENDED \
                         -D_ALL_SOURCE \
                         -D_LARGE_TIME_API \
                         -D_OPEN_SYS_FILE_EXT \
                         -DPATH_MAX=255 \
                         -qCHARS=signed \
                         -qXPLINK \
                         -qFLOAT=IEEE
endif

if AIX
libuv_la_CFLAGS += -D_ALL_SOURCE \
                   -D_XOPEN_SOURCE=500 \
                   -D_LINUX_SOURCE_COMPAT \
                   -D_THREAD_SAFE \
                   -DHAVE_SYS_AHAFS_EVPRODS_H
uvinclude_HEADERS += include/uv/aix.h
libuv_la_SOURCES += src/unix/aix.c src/unix/aix-common.c
endif

if OS400
libuv_la_CFLAGS += -D_ALL_SOURCE \
                   -D_XOPEN_SOURCE=500 \
                   -D_LINUX_SOURCE_COMPAT \
                   -D_THREAD_SAFE
uvinclude_HEADERS += include/uv/posix.h
libuv_la_SOURCES += src/unix/aix-common.c \
                    src/unix/ibmi.c \
                    src/unix/posix-poll.c \
                    src/unix/no-fsevents.c
endif

if ANDROID
libuv_la_CFLAGS += -D_GNU_SOURCE
endif

if CYGWIN
uvinclude_HEADERS += include/uv/posix.h
libuv_la_CFLAGS += -D_GNU_SOURCE
libuv_la_SOURCES += src/unix/cygwin.c \
                    src/unix/bsd-ifaddrs.c \
                    src/unix/no-fsevents.c \
                    src/unix/no-proctitle.c \
                    src/unix/posix-hrtime.c \
                    src/unix/posix-poll.c \
                    src/unix/procfs-exepath.c \
                    src/unix/sysinfo-loadavg.c \
                    src/unix/sysinfo-memory.c
endif

if DARWIN
uvinclude_HEADERS += include/uv/darwin.h
libuv_la_CFLAGS += -D_DARWIN_USE_64_BIT_INODE=1
libuv_la_CFLAGS += -D_DARWIN_UNLIMITED_SELECT=1
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/darwin-proctitle.c \
                    src/unix/darwin-stub.h \
                    src/unix/darwin.c \
                    src/unix/fsevents.c \
                    src/unix/kqueue.c \
                    src/unix/proctitle.c \
                    src/unix/random-getentropy.c
test_run_tests_LDFLAGS += -lutil
endif

if DRAGONFLY
uvinclude_HEADERS += include/uv/bsd.h
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/bsd-proctitle.c \
                    src/unix/freebsd.c \
                    src/unix/kqueue.c \
                    src/unix/posix-hrtime.c
test_run_tests_LDFLAGS += -lutil
endif

if FREEBSD
uvinclude_HEADERS += include/uv/bsd.h
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/bsd-proctitle.c \
                    src/unix/freebsd.c \
                    src/unix/kqueue.c \
                    src/unix/posix-hrtime.c \
                    src/unix/random-getrandom.c
test_run_tests_LDFLAGS += -lutil
endif

if HAIKU
uvinclude_HEADERS += include/uv/posix.h
libuv_la_CFLAGS += -D_BSD_SOURCE
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/haiku.c \
                    src/unix/no-fsevents.c \
                    src/unix/no-proctitle.c \
                    src/unix/posix-hrtime.c \
                    src/unix/posix-poll.c
endif

if HURD
uvinclude_HEADERS += include/uv/posix.h
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/no-fsevents.c \
                    src/unix/no-proctitle.c \
                    src/unix/posix-hrtime.c \
                    src/unix/posix-poll.c \
                    src/unix/hurd.c
endif

if LINUX
uvinclude_HEADERS += include/uv/linux.h
libuv_la_CFLAGS += -D_GNU_SOURCE
libuv_la_SOURCES += src/unix/linux.c \
                    src/unix/procfs-exepath.c \
                    src/unix/proctitle.c \
                    src/unix/random-getrandom.c \
                    src/unix/random-sysctl-linux.c
test_run_tests_LDFLAGS += -lutil
endif

if MSYS
libuv_la_CFLAGS += -D_GNU_SOURCE
libuv_la_SOURCES += src/unix/cygwin.c \
                    src/unix/bsd-ifaddrs.c \
                    src/unix/no-fsevents.c \
                    src/unix/no-proctitle.c \
                    src/unix/posix-hrtime.c \
                    src/unix/posix-poll.c \
                    src/unix/procfs-exepath.c \
                    src/unix/sysinfo-loadavg.c \
                    src/unix/sysinfo-memory.c
endif

if NETBSD
uvinclude_HEADERS += include/uv/bsd.h
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/bsd-proctitle.c \
                    src/unix/kqueue.c \
                    src/unix/netbsd.c \
                    src/unix/posix-hrtime.c
test_run_tests_LDFLAGS += -lutil
endif

if OPENBSD
uvinclude_HEADERS += include/uv/bsd.h
libuv_la_SOURCES += src/unix/bsd-ifaddrs.c \
                    src/unix/bsd-proctitle.c \
                    src/unix/kqueue.c \
                    src/unix/openbsd.c \
                    src/unix/posix-hrtime.c \
                    src/unix/random-getentropy.c
test_run_tests_LDFLAGS += -lutil
endif

if SUNOS
uvinclude_HEADERS += include/uv/sunos.h
libuv_la_CFLAGS += -D__EXTENSIONS__ \
                   -D_XOPEN_SOURCE=500 \
                   -D_REENTRANT
libuv_la_SOURCES += src/unix/no-proctitle.c \
                    src/unix/sunos.c
endif

if OS390
libuv_la_CFLAGS += -D_UNIX03_THREADS \
                   -D_UNIX03_SOURCE \
                   -D_OPEN_SYS_IF_EXT=1 \
                   -D_OPEN_MSGQ_EXT \
                   -D_XOPEN_SOURCE_EXTENDED \
                   -D_ALL_SOURCE \
                   -D_LARGE_TIME_API \
                   -D_OPEN_SYS_SOCK_EXT3 \
                   -D_OPEN_SYS_SOCK_IPV6 \
                   -D_OPEN_SYS_FILE_EXT \
                   -DUV_PLATFORM_SEM_T=int \
                   -DPATH_MAX=255 \
                   -qCHARS=signed \
                   -qXPLINK \
                   -qFLOAT=IEEE
libuv_la_LDFLAGS += -qXPLINK
libuv_la_SOURCES += src/unix/os390.c \
                    src/unix/os390-syscalls.c \
                    src/unix/proctitle.c
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = @PACKAGE_NAME@.pc
