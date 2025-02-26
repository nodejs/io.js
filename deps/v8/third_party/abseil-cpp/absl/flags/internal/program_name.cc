//
//  Copyright 2019 The Abseil Authors.
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

#include "absl/flags/internal/program_name.h"

#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/internal/path_util.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace flags_internal {

static absl::Mutex* ProgramNameMutex() {
  static absl::NoDestructor<absl::Mutex> mutex;
  return mutex.get();
}
ABSL_CONST_INIT static std::string* program_name ABSL_GUARDED_BY(
    ProgramNameMutex()) ABSL_PT_GUARDED_BY(ProgramNameMutex()) = nullptr;

std::string ProgramInvocationName() {
  absl::MutexLock l(ProgramNameMutex());
  return program_name ? *program_name : "UNKNOWN";
}

std::string ShortProgramInvocationName() {
  absl::MutexLock l(ProgramNameMutex());
  return program_name ? std::string(flags_internal::Basename(*program_name))
                      : "UNKNOWN";
}

void SetProgramInvocationName(absl::string_view prog_name_str) {
  absl::MutexLock l(ProgramNameMutex());
  if (!program_name) {
    program_name = new std::string(prog_name_str);
  } else {
    program_name->assign(prog_name_str.data(), prog_name_str.size());
  }
}

}  // namespace flags_internal
ABSL_NAMESPACE_END
}  // namespace absl
