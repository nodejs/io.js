// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: log/absl_log.h
// -----------------------------------------------------------------------------
//
// This header declares a family of `ABSL_LOG` macros as alternative spellings
// for macros in `log.h`.
//
// Basic invocation looks like this:
//
//   ABSL_LOG(INFO) << "Found " << num_cookies << " cookies";
//
// Most `ABSL_LOG` macros take a severity level argument. The severity levels
// are `INFO`, `WARNING`, `ERROR`, and `FATAL`.
//
// For full documentation, see comments in `log.h`, which includes full
// reference documentation on use of the equivalent `LOG` macro and has an
// identical set of macros without the ABSL_* prefix.

#ifndef ABSL_LOG_ABSL_LOG_H_
#define ABSL_LOG_ABSL_LOG_H_

#include "absl/log/internal/log_impl.h"

#define ABSL_LOG(severity) ABSL_LOG_INTERNAL_LOG_IMPL(_##severity)
#define ABSL_PLOG(severity) ABSL_LOG_INTERNAL_PLOG_IMPL(_##severity)
#define ABSL_DLOG(severity) ABSL_LOG_INTERNAL_DLOG_IMPL(_##severity)

#define ABSL_VLOG(verbose_level) ABSL_LOG_INTERNAL_VLOG_IMPL(verbose_level)
#define ABSL_DVLOG(verbose_level) ABSL_LOG_INTERNAL_DVLOG_IMPL(verbose_level)

#define ABSL_LOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_LOG_IF_IMPL(_##severity, condition)
#define ABSL_PLOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_PLOG_IF_IMPL(_##severity, condition)
#define ABSL_DLOG_IF(severity, condition) \
  ABSL_LOG_INTERNAL_DLOG_IF_IMPL(_##severity, condition)

#define ABSL_LOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_LOG_EVERY_N_IMPL(_##severity, n)
#define ABSL_LOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_LOG_FIRST_N_IMPL(_##severity, n)
#define ABSL_LOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_LOG_EVERY_POW_2_IMPL(_##severity)
#define ABSL_LOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_LOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define ABSL_PLOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_N_IMPL(_##severity, n)
#define ABSL_PLOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_PLOG_FIRST_N_IMPL(_##severity, n)
#define ABSL_PLOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_POW_2_IMPL(_##severity)
#define ABSL_PLOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_PLOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define ABSL_DLOG_EVERY_N(severity, n) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_N_IMPL(_##severity, n)
#define ABSL_DLOG_FIRST_N(severity, n) \
  ABSL_LOG_INTERNAL_DLOG_FIRST_N_IMPL(_##severity, n)
#define ABSL_DLOG_EVERY_POW_2(severity) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_POW_2_IMPL(_##severity)
#define ABSL_DLOG_EVERY_N_SEC(severity, n_seconds) \
  ABSL_LOG_INTERNAL_DLOG_EVERY_N_SEC_IMPL(_##severity, n_seconds)

#define ABSL_VLOG_EVERY_N(verbose_level, n) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_N_IMPL(verbose_level, n)
#define ABSL_VLOG_FIRST_N(verbose_level, n) \
  ABSL_LOG_INTERNAL_VLOG_FIRST_N_IMPL(verbose_level, n)
#define ABSL_VLOG_EVERY_POW_2(verbose_level, n) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_POW_2_IMPL(verbose_level, n)
#define ABSL_VLOG_EVERY_N_SEC(verbose_level, n) \
  ABSL_LOG_INTERNAL_VLOG_EVERY_N_SEC_IMPL(verbose_level, n)

#define ABSL_LOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define ABSL_LOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_LOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define ABSL_LOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define ABSL_LOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_LOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#define ABSL_PLOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define ABSL_PLOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_PLOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define ABSL_PLOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define ABSL_PLOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_PLOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#define ABSL_DLOG_IF_EVERY_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_N_IMPL(_##severity, condition, n)
#define ABSL_DLOG_IF_FIRST_N(severity, condition, n) \
  ABSL_LOG_INTERNAL_DLOG_IF_FIRST_N_IMPL(_##severity, condition, n)
#define ABSL_DLOG_IF_EVERY_POW_2(severity, condition) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_POW_2_IMPL(_##severity, condition)
#define ABSL_DLOG_IF_EVERY_N_SEC(severity, condition, n_seconds) \
  ABSL_LOG_INTERNAL_DLOG_IF_EVERY_N_SEC_IMPL(_##severity, condition, n_seconds)

#endif  // ABSL_LOG_ABSL_LOG_H_
