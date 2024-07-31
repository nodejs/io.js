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

// This file is used to test the mismatch of the flag type between definition
// and declaration. These are definitions. flag_test.cc contains declarations.
#include <string>
#include "absl/flags/flag.h"

ABSL_FLAG(int, mistyped_int_flag, 0, "");
ABSL_FLAG(std::string, mistyped_string_flag, "", "");
ABSL_FLAG(bool, flag_on_separate_file, false, "");
ABSL_RETIRED_FLAG(bool, retired_flag_on_separate_file, false, "");
