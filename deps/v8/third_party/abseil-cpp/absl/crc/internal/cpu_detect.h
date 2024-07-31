// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_CRC_INTERNAL_CPU_DETECT_H_
#define ABSL_CRC_INTERNAL_CPU_DETECT_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

// Enumeration of architectures that we have special-case tuning parameters for.
// This set may change over time.
enum class CpuType {
  kUnknown,
  kIntelHaswell,
  kAmdRome,
  kAmdNaples,
  kAmdMilan,
  kAmdGenoa,
  kAmdRyzenV3000,
  kIntelCascadelakeXeon,
  kIntelSkylakeXeon,
  kIntelBroadwell,
  kIntelSkylake,
  kIntelIvybridge,
  kIntelSandybridge,
  kIntelWestmere,
  kArmNeoverseN1,
  kArmNeoverseV1,
  kAmpereSiryn,
  kArmNeoverseN2,
  kArmNeoverseV2
};

// Returns the type of host CPU this code is running on.  Returns kUnknown if
// the host CPU is of unknown type, or if detection otherwise fails.
CpuType GetCpuType();

// Returns whether the host CPU supports the CPU features needed for our
// accelerated implementations. The CpuTypes enumerated above apart from
// kUnknown support the required features. On unknown CPUs, we can use
// this to see if it's safe to use hardware acceleration, though without any
// tuning.
bool SupportsArmCRC32PMULL();

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_CPU_DETECT_H_
