// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#if !defined(_WIN32)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "libreprl.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Well-known file descriptor numbers for reprl <-> child communication, child process side
#define REPRL_CHILD_CTRL_IN 100
#define REPRL_CHILD_CTRL_OUT 101
#define REPRL_CHILD_DATA_IN 102
#define REPRL_CHILD_DATA_OUT 103

/// Maximum timeout in microseconds. Mostly just limited by the fact that the timeout in milliseconds has to fit into a 32-bit integer.
#define REPRL_MAX_TIMEOUT_IN_MICROSECONDS ((uint64_t)(INT_MAX) * 1000)

static size_t min(size_t x, size_t y) {
  return x < y ? x : y;
}

#ifdef __linux__
// This function creates the UID/GID mapping that we need inside of the user
// namespace. This is needed such that the files we create have a proper owner
// attached to them.
static void write_id_maps(uid_t uid, gid_t gid) {
    char setgroups_path[] = "/proc/self/setgroups";
    char uid_map_path[] = "/proc/self/uid_map";
    char gid_map_path[] = "/proc/self/gid_map";

    int setgroups_fd = open(setgroups_path, O_WRONLY);
    int uid_map_fd = open(uid_map_path, O_WRONLY);
    int gid_map_fd = open(gid_map_path, O_WRONLY);

    if (setgroups_fd == -1 || uid_map_fd == -1 || gid_map_fd == -1) {
        fprintf(stderr, "Error opening setgroups/uid_map/gid_map file: %s\n", strerror(errno));
        _exit(-1);
    }

    // More context on this: https://lwn.net/Articles/626665/
    dprintf(setgroups_fd, "deny");
    dprintf(uid_map_fd, "%d %d 1", uid, uid);
    dprintf(gid_map_fd, "%d %d 1", gid, gid);

    close(setgroups_fd);
    close(uid_map_fd);
    close(gid_map_fd);
}

// Creates a tmpfs at `mount_point` in a new user namespace.
static void create_tmpfs(const char* mount_point) {
    // Get the UID and GID before we call unshare.
    uid_t uid = getuid();
    gid_t gid = getgid();

    // We create a new user (CLONE_NEWUSER) and mount (CLONE_NEWNS)
    // namespace here such that we can mount our own tmpfs onto
    // mount_point that is only visible to this process.
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1) {
        fprintf(stderr, "unshare failed to create a new mount namespace in the child: %s\n", strerror(errno));
        _exit(-1);
    };

    // Now write the UID / GID mappings
    write_id_maps(uid, gid);

    // Mount a new tmpfs onto `mount_point` this allows us to add files
    // here that get automatically cleaned up once the process exits.
    if (mount("tmpfs", mount_point, "tmpfs", 0, NULL) == -1) {
        fprintf(stderr, "mount failed to create a tmpfs in namespace in the child: %s\n", strerror(errno));
        _exit(-1);
    }
}
#endif

static uint64_t current_usecs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static char** copy_string_array(const char** orig)
{
    size_t num_entries = 0;
    for (const char** current = orig; *current; current++) {
        num_entries += 1;
    }
    char** copy = calloc(num_entries + 1, sizeof(char*));
    for (size_t i = 0; i < num_entries; i++) {
        copy[i] = strdup(orig[i]);
    }
    return copy;
}

static void free_string_array(char** arr)
{
    if (!arr) return;
    for (char** current = arr; *current; current++) {
        free(*current);
    }
    free(arr);
}

// A unidirectional communication channel for larger amounts of data, up to a maximum size (REPRL_MAX_DATA_SIZE).
// Implemented as a (RAM-backed) file for which the file descriptor is shared with the child process and which is mapped into our address space.
struct data_channel {
    // File descriptor of the underlying file. Directly shared with the child process.
    int fd;
    // Memory mapping of the file, always of size REPRL_MAX_DATA_SIZE.
    char* mapping;
};

struct reprl_context {
    // Whether reprl_initialize has been successfully performed on this context.
    int initialized;

    // Read file descriptor of the control pipe. Only valid if a child process is running (i.e. pid is nonzero).
    int ctrl_in;
    // Write file descriptor of the control pipe. Only valid if a child process is running (i.e. pid is nonzero).
    int ctrl_out;

    // Data channel REPRL -> Child
    struct data_channel* data_in;
    // Data channel Child -> REPRL
    struct data_channel* data_out;

    // Optional data channel for the child's stdout and stderr.
    struct data_channel* child_stdout;
    struct data_channel* child_stderr;

    // PID of the child process. Will be zero if no child process is currently running.
    pid_t pid;

    // Arguments and environment for the child process.
    char** argv;
    char** envp;

    // A malloc'd string containing a description of the last error that occurred.
    char* last_error;
};

static int reprl_error(struct reprl_context* ctx, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    free(ctx->last_error);
    vasprintf(&ctx->last_error, format, args);
    return -1;
}

static struct data_channel* reprl_create_data_channel(struct reprl_context* ctx)
{
#ifdef __linux__
    int fd = memfd_create("REPRL_DATA_CHANNEL", MFD_CLOEXEC);
#else
    char path[] = "/tmp/reprl_data_channel_XXXXXXXX";
    int fd = mkostemp(path, O_CLOEXEC);
    unlink(path);
#endif
    if (fd == -1 || ftruncate(fd, REPRL_MAX_DATA_SIZE) != 0) {
        reprl_error(ctx, "Failed to create data channel file: %s", strerror(errno));
        return NULL;
    }
    char* mapping = mmap(0, REPRL_MAX_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        reprl_error(ctx, "Failed to mmap data channel file: %s", strerror(errno));
        return NULL;
    }

    struct data_channel* channel = malloc(sizeof(struct data_channel));
    channel->fd = fd;
    channel->mapping = mapping;
    return channel;
}

static void reprl_destroy_data_channel(struct data_channel* channel)
{
    if (!channel) return;
    close(channel->fd);
    munmap(channel->mapping, REPRL_MAX_DATA_SIZE);
    free(channel);
}

static void reprl_child_terminated(struct reprl_context* ctx)
{
    if (!ctx->pid) return;
    ctx->pid = 0;
    close(ctx->ctrl_in);
    close(ctx->ctrl_out);
}

static void reprl_terminate_child(struct reprl_context* ctx)
{
    if (!ctx->pid) return;
    int status;
    kill(ctx->pid, SIGKILL);
    waitpid(ctx->pid, &status, 0);
    reprl_child_terminated(ctx);
}

static int reprl_spawn_child(struct reprl_context* ctx)
{
    // This is also a good time to ensure the data channel backing files don't grow too large.
    ftruncate(ctx->data_in->fd, REPRL_MAX_DATA_SIZE);
    ftruncate(ctx->data_out->fd, REPRL_MAX_DATA_SIZE);
    if (ctx->child_stdout) ftruncate(ctx->child_stdout->fd, REPRL_MAX_DATA_SIZE);
    if (ctx->child_stderr) ftruncate(ctx->child_stderr->fd, REPRL_MAX_DATA_SIZE);

    int crpipe[2] = { 0, 0 };          // control pipe child -> reprl
    int cwpipe[2] = { 0, 0 };          // control pipe reprl -> child

    if (pipe(crpipe) != 0) {
        return reprl_error(ctx, "Could not create pipe for REPRL communication: %s", strerror(errno));
    }
    if (pipe(cwpipe) != 0) {
        close(crpipe[0]);
        close(crpipe[1]);
        return reprl_error(ctx, "Could not create pipe for REPRL communication: %s", strerror(errno));
    }

    ctx->ctrl_in = crpipe[0];
    ctx->ctrl_out = cwpipe[1];
    fcntl(ctx->ctrl_in, F_SETFD, FD_CLOEXEC);
    fcntl(ctx->ctrl_out, F_SETFD, FD_CLOEXEC);

#ifdef __linux__
    // This is where we will mount our own tmpfs, this is intended to be used
    // for targets like Chrome, where we have to pass the user data directory.
    // Even if the target does not clean up after themselves, the tmpfs in the
    // user namespace will be removed once the process exits. Also, every child
    // process, i.e. fuzzing instance, can then have it's own tmpfs.
    // This only works on Linux right now, which is where we fuzz Chrome, this
    // won't work on any other OS.
    const char mount_point[] = "/tmp/fuzzilli_tmp";

    // Create the mountpoint for our tmpfs here. This is just an empty dir.
    // We also do not really care if this directory exists, we just need it as
    // a mountpoint.
    if (mkdir(mount_point, 0)) {
        if (errno != EEXIST) {
          fprintf(stderr, "mkdir failed to create %s to create a mountpoint: %s\n", mount_point, strerror(errno));
        }
    }
#endif

#ifdef __linux__
    // Use vfork() on Linux as that considerably improves the fuzzer performance. See also https://github.com/googleprojectzero/fuzzilli/issues/174
    // Due to vfork, the code executed in the child process *must not* modify any memory apart from its stack, as it will share the page table of its parent.
    pid_t pid = vfork();
#else
    pid_t pid = fork();
#endif
    if (pid == 0) {
        if (dup2(cwpipe[0], REPRL_CHILD_CTRL_IN) < 0 ||
            dup2(crpipe[1], REPRL_CHILD_CTRL_OUT) < 0 ||
            dup2(ctx->data_out->fd, REPRL_CHILD_DATA_IN) < 0 ||
            dup2(ctx->data_in->fd, REPRL_CHILD_DATA_OUT) < 0) {
            fprintf(stderr, "dup2 failed in the child: %s\n", strerror(errno));
            _exit(-1);
        }

#ifdef __linux__
        // Set RLIMIT_CORE to 0, such that we don't produce core dumps. The
        // added benefit of doing this here, in the child process, is that we
        // can still get core dumps when Fuzzilli crashes.
        struct rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        if (setrlimit(RLIMIT_CORE, &core_limit) < 0) {
            fprintf(stderr, "setrlimit failed in the child: %s\n", strerror(errno));
            _exit(-1);
        };
#endif

        // Unblock any blocked signals. It seems that libdispatch sometimes blocks delivery of certain signals.
        sigset_t newset;
        sigemptyset(&newset);
        if (sigprocmask(SIG_SETMASK, &newset, NULL) != 0) {
            fprintf(stderr, "sigprocmask failed in the child: %s\n", strerror(errno));
            _exit(-1);
        }

        close(cwpipe[0]);
        close(crpipe[1]);

        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, 0);
        if (ctx->child_stdout) dup2(ctx->child_stdout->fd, 1);
        else dup2(devnull, 1);
        if (ctx->child_stderr) dup2(ctx->child_stderr->fd, 2);
        else dup2(devnull, 2);
        close(devnull);

#ifdef __linux__
        // Create the tmpfs at the specific mount point here in the child process
        // such that we have a tmpfs for this process only that will be cleaned up at process exit.
        // This will also write into the necessary files in /proc, so we need to do this here after we've fork()'ed.
        // This will only work on Linux, see the comment above where call mkdir.
        create_tmpfs(mount_point);
#endif

        // close all other FDs. We try to use FD_CLOEXEC everywhere, but let's be extra sure we don't leak any fds to the child.
        int tablesize = getdtablesize();
        for (int i = 3; i < tablesize; i++) {
            if (i == REPRL_CHILD_CTRL_IN || i == REPRL_CHILD_CTRL_OUT || i == REPRL_CHILD_DATA_IN || i == REPRL_CHILD_DATA_OUT) {
                continue;
            }
            close(i);
        }

        execve(ctx->argv[0], ctx->argv, ctx->envp);

        fprintf(stderr, "Failed to execute child process %s: %s\n", ctx->argv[0], strerror(errno));
        fflush(stderr);
        _exit(-1);
    }

    close(crpipe[1]);
    close(cwpipe[0]);

    if (pid < 0) {
        close(ctx->ctrl_in);
        close(ctx->ctrl_out);
        return reprl_error(ctx, "Failed to fork: %s", strerror(errno));
    }
    ctx->pid = pid;

    char helo[5] = { 0 };
    if (read(ctx->ctrl_in, helo, 4) != 4) {
        reprl_terminate_child(ctx);
        return reprl_error(ctx, "Did not receive HELO message from child: %s", strerror(errno));
    }

    if (strncmp(helo, "HELO", 4) != 0) {
        reprl_terminate_child(ctx);
        return reprl_error(ctx, "Received invalid HELO message from child: %s", helo);
    }

    if (write(ctx->ctrl_out, helo, 4) != 4) {
        reprl_terminate_child(ctx);
        return reprl_error(ctx, "Failed to send HELO reply message to child: %s", strerror(errno));
    }

#ifdef __linux__
    struct rlimit core_limit = {};
    if (prlimit(pid, RLIMIT_CORE, NULL, &core_limit) < 0) {
        reprl_terminate_child(ctx);
        return reprl_error(ctx, "prlimit failed: %s\n", strerror(errno));
    }
    if (core_limit.rlim_cur != 0 || core_limit.rlim_max != 0) {
        reprl_terminate_child(ctx);
        return reprl_error(ctx, "Detected non-zero RLIMIT_CORE. Check that the child does not set RLIMIT_CORE manually.\n");
    }
#endif

    return 0;
}

struct reprl_context* reprl_create_context()
{
    // "Reserve" the well-known REPRL fds so no other fd collides with them.
    // This would cause various kinds of issues in reprl_spawn_child.
    // It would be enough to do this once per process in the case of multiple
    // REPRL instances, but it's probably not worth the implementation effort.
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, REPRL_CHILD_CTRL_IN);
    dup2(devnull, REPRL_CHILD_CTRL_OUT);
    dup2(devnull, REPRL_CHILD_DATA_IN);
    dup2(devnull, REPRL_CHILD_DATA_OUT);
    close(devnull);

    return calloc(1, sizeof(struct reprl_context));
}

int reprl_initialize_context(struct reprl_context* ctx, const char** argv, const char** envp, int capture_stdout, int capture_stderr)
{
    if (ctx->initialized) {
        return reprl_error(ctx, "Context is already initialized");
    }

    // We need to ignore SIGPIPE since we could end up writing to a pipe after our child process has exited.
    signal(SIGPIPE, SIG_IGN);

    ctx->argv = copy_string_array(argv);
    ctx->envp = copy_string_array(envp);

    ctx->data_in = reprl_create_data_channel(ctx);
    ctx->data_out = reprl_create_data_channel(ctx);
    if (capture_stdout) {
        ctx->child_stdout = reprl_create_data_channel(ctx);
    }
    if (capture_stderr) {
        ctx->child_stderr = reprl_create_data_channel(ctx);
    }
    if (!ctx->data_in || !ctx->data_out || (capture_stdout && !ctx->child_stdout) || (capture_stderr && !ctx->child_stderr)) {
        // Proper error message will have been set by reprl_create_data_channel
        return -1;
    }

    ctx->initialized = 1;
    return 0;
}

void reprl_destroy_context(struct reprl_context* ctx)
{
    reprl_terminate_child(ctx);

    free_string_array(ctx->argv);
    free_string_array(ctx->envp);

    reprl_destroy_data_channel(ctx->data_in);
    reprl_destroy_data_channel(ctx->data_out);
    reprl_destroy_data_channel(ctx->child_stdout);
    reprl_destroy_data_channel(ctx->child_stderr);

    free(ctx->last_error);
    free(ctx);
}

int reprl_execute(struct reprl_context* ctx, const char* script, uint64_t script_size, uint64_t timeout, uint64_t* execution_time, int fresh_instance)
{
    if (!ctx->initialized) {
        return reprl_error(ctx, "REPRL context is not initialized");
    }

    if (script_size > REPRL_MAX_DATA_SIZE) {
        return reprl_error(ctx, "Script too large");
    }

    if (timeout > REPRL_MAX_TIMEOUT_IN_MICROSECONDS) {
        return reprl_error(ctx, "Timeout too large");
    }
    int timeout_ms = (int)(timeout / 1000);

    // Terminate any existing instance if requested.
    if (fresh_instance && ctx->pid) {
        reprl_terminate_child(ctx);
    }

    // Reset file position so the child can simply read(2) and write(2) to these fds.
    lseek(ctx->data_out->fd, 0, SEEK_SET);
    lseek(ctx->data_in->fd, 0, SEEK_SET);
    if (ctx->child_stdout) {
        lseek(ctx->child_stdout->fd, 0, SEEK_SET);
    }
    if (ctx->child_stderr) {
        lseek(ctx->child_stderr->fd, 0, SEEK_SET);
    }

    // Spawn a new instance if necessary.
    if (!ctx->pid) {
        int r = reprl_spawn_child(ctx);
        if (r != 0) return r;
    }

    // Copy the script to the data channel.
    memcpy(ctx->data_out->mapping, script, script_size);

    // Tell child to execute the script.
    if (write(ctx->ctrl_out, "exec", 4) != 4 ||
        write(ctx->ctrl_out, &script_size, 8) != 8) {
        // These can fail if the child unexpectedly terminated between executions.
        // Check for that here to be able to provide a better error message.
        int status;
        if (waitpid(ctx->pid, &status, WNOHANG) == ctx->pid) {
            reprl_child_terminated(ctx);
            if (WIFEXITED(status)) {
                return reprl_error(ctx, "Child unexpectedly exited with status %i between executions", WEXITSTATUS(status));
            } else {
                return reprl_error(ctx, "Child unexpectedly terminated with signal %i between executions", WTERMSIG(status));
            }
        }
        return reprl_error(ctx, "Failed to send command to child process: %s", strerror(errno));
    }

    // Wait for child to finish execution (or crash).
    uint64_t start_time = current_usecs();
    struct pollfd fds = {.fd = ctx->ctrl_in, .events = POLLIN, .revents = 0};
    int res = poll(&fds, 1, timeout_ms);
    *execution_time = current_usecs() - start_time;
    if (res == 0) {
        // Execution timed out. Kill child and return a timeout status.
        reprl_terminate_child(ctx);
        return 1 << 16;
    } else if (res != 1) {
        // An error occurred.
        // We expect all signal handlers to be installed with SA_RESTART, so receiving EINTR here is unexpected and thus also an error.
        return reprl_error(ctx, "Failed to poll: %s", strerror(errno));
    }

    // Poll succeeded, so there must be something to read now (either the status or EOF).
    int status;
    ssize_t rv = read(ctx->ctrl_in, &status, 4);
    if (rv < 0) {
        return reprl_error(ctx, "Failed to read from control pipe: %s", strerror(errno));
    } else if (rv != 4) {
        // Most likely, the child process crashed and closed the write end of the control pipe.
        // Unfortunately, there probably is nothing that guarantees that waitpid() will immediately succeed now,
        // and we also don't want to block here. So just retry waitpid() a few times...
        int success = 0;
        do {
            success = waitpid(ctx->pid, &status, WNOHANG) == ctx->pid;
            if (!success) usleep(10);
        } while (!success && current_usecs() - start_time < timeout);

        if (!success) {
            // Wait failed, so something weird must have happened. Maybe somehow the control pipe was closed without the child exiting?
            // Probably the best we can do is kill the child and return an error.
            reprl_terminate_child(ctx);
            return reprl_error(ctx, "Child in weird state after execution");
        }

        // Cleanup any state related to this child process.
        reprl_child_terminated(ctx);

        if (WIFEXITED(status)) {
            status = WEXITSTATUS(status) << 8;
        } else if (WIFSIGNALED(status)) {
            status = WTERMSIG(status);
        } else {
            // This shouldn't happen, since we don't specify WUNTRACED for waitpid...
            return reprl_error(ctx, "Waitpid returned unexpected child state %i", status);
        }
    }

    // The status must be a positive number, see the status encoding format below.
    // We also don't allow the child process to indicate a timeout. If we wanted,
    // we could treat it as an error if the upper bits are set.
    status &= 0xffff;

    return status;
}

static const char* fetch_data_channel_content(struct data_channel* channel)
{
    if (!channel) return "";
    size_t pos = lseek(channel->fd, 0, SEEK_CUR);
    pos = min(pos, REPRL_MAX_DATA_SIZE - 1);
    channel->mapping[pos] = 0;
    return channel->mapping;
}

const char* reprl_fetch_fuzzout(struct reprl_context* ctx)
{
    return fetch_data_channel_content(ctx->data_in);
}

const char* reprl_fetch_stdout(struct reprl_context* ctx)
{
    return fetch_data_channel_content(ctx->child_stdout);
}

const char* reprl_fetch_stderr(struct reprl_context* ctx)
{
    return fetch_data_channel_content(ctx->child_stderr);
}

const char* reprl_get_last_error(struct reprl_context* ctx)
{
    return ctx->last_error;
}

#endif
