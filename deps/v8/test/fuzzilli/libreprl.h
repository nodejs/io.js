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

#ifndef LIBREPRL_H
#define LIBREPRL_H

#include <limits.h>
#include <stdint.h>

/// Maximum size for data transferred through REPRL. In particular, this is the
/// maximum size of scripts that can be executed. Currently, this is 16MB.
/// Executing a 16MB script file is very likely to take longer than the typical
/// timeout, so the limit on script size shouldn't be a problem in practice.
#define REPRL_MAX_DATA_SIZE (16 << 20)

/// Opaque struct representing a REPRL execution context.
struct reprl_context;

/// Allocates a new REPRL context.
/// @return an uninitialzed REPRL context
struct reprl_context* reprl_create_context();

/// Initializes a REPRL context.
///
/// @param ctx An uninitialized context
/// @param argv The argv vector for the child processes
/// @param envp The envp vector for the child processes
/// @param capture_stdout Whether this REPRL context should capture the child's
/// stdout
/// @param capture_stderr Whether this REPRL context should capture the child's
/// stderr
/// @return zero in case of no errors, otherwise a negative value
int reprl_initialize_context(struct reprl_context* ctx, const char** argv,
                             const char** envp, int capture_stdout,
                             int capture_stderr);

/// Destroys a REPRL context, freeing all resources held by it.
///
/// @param ctx The context to destroy
void reprl_destroy_context(struct reprl_context* ctx);

/// Executes the provided script in the target process, wait for its completion,
/// and return the result. If necessary, or if fresh_instance is true, this will
/// automatically spawn a new instance of the target process.
///
/// @param ctx The REPRL context
/// @param script The script to execute as utf-8 encoded data
/// @param script_size Size of the script as number of bytes
/// @param timeout The maximum allowed execution time in microseconds
/// @param execution_time A pointer to which, if execution succeeds, the
/// execution time in microseconds is written to
/// @param fresh_instance if true, forces the creation of a new instance of the
/// target
/// @return A REPRL exit status (see below) or a negative number in case of an
/// error
int reprl_execute(struct reprl_context* ctx, const char* script,
                  uint64_t script_size, uint64_t timeout,
                  uint64_t* execution_time, int fresh_instance);

/// Returns true if the execution terminated due to a signal.
///
/// The 32bit REPRL exit status as returned by reprl_execute has the following
/// format:
///     [ 00000000 | did_timeout | exit_code | terminating_signal ]
/// Only one of did_timeout, exit_code, or terminating_signal may be set at one
/// time.
static inline int RIFSIGNALED(int status) { return (status & 0xff) != 0; }

/// Returns true if the execution terminated due to a timeout.
static inline int RIFTIMEDOUT(int status) { return (status & 0xff0000) != 0; }

/// Returns true if the execution finished normally.
static inline int RIFEXITED(int status) {
  return !RIFSIGNALED(status) && !RIFTIMEDOUT(status);
}

/// Returns the terminating signal in case RIFSIGNALED is true.
static inline int RTERMSIG(int status) { return status & 0xff; }

/// Returns the exit status in case RIFEXITED is true.
static inline int REXITSTATUS(int status) { return (status >> 8) & 0xff; }

/// Returns the stdout data of the last successful execution if the context is
/// capturing stdout, otherwise an empty string. The output is limited to
/// REPRL_MAX_DATA_SIZE (currently 16MB).
///
/// @param ctx The REPRL context
/// @return A string pointer which is owned by the REPRL context and thus should
/// not be freed by the caller
const char* reprl_fetch_stdout(struct reprl_context* ctx);

/// Returns the stderr data of the last successful execution if the context is
/// capturing stderr, otherwise an empty string. The output is limited to
/// REPRL_MAX_DATA_SIZE (currently 16MB).
///
/// @param ctx The REPRL context
/// @return A string pointer which is owned by the REPRL context and thus should
/// not be freed by the caller
const char* reprl_fetch_stderr(struct reprl_context* ctx);

/// Returns the fuzzout data of the last successful execution.
/// The output is limited to REPRL_MAX_DATA_SIZE (currently 16MB).
///
/// @param ctx The REPRL context
/// @return A string pointer which is owned by the REPRL context and thus should
/// not be freed by the caller
const char* reprl_fetch_fuzzout(struct reprl_context* ctx);

/// Returns a string describing the last error that occurred in the given
/// context.
///
/// @param ctx The REPRL context
/// @return A string pointer which is owned by the REPRL context and thus should
/// not be freed by the caller
const char* reprl_get_last_error(struct reprl_context* ctx);

#endif
