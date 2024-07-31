// Copyright 2017 The Abseil Authors.
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

#include "absl/random/internal/seed_material.h"

#include <fcntl.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#if defined(__native_client__)

#include <nacl/nacl_random.h>
#define ABSL_RANDOM_USE_NACL_SECURE_RANDOM 1

#elif defined(_WIN32)

#include <windows.h>
#define ABSL_RANDOM_USE_BCRYPT 1
#pragma comment(lib, "bcrypt.lib")

#elif defined(__Fuchsia__)
#include <zircon/syscalls.h>

#endif

#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
// glibc >= 2.25 has getentropy()
#define ABSL_RANDOM_USE_GET_ENTROPY 1
#endif

#if defined(__EMSCRIPTEN__)
#include <sys/random.h>
// Emscripten has getentropy, but it resides in a different header.
#define ABSL_RANDOM_USE_GET_ENTROPY 1
#endif

#if defined(ABSL_RANDOM_USE_BCRYPT)
#include <bcrypt.h>

#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
// Also link bcrypt; this can be done via linker options or:
// #pragma comment(lib, "bcrypt.lib")
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

// Read OS Entropy for random number seeds.
// TODO(absl-team): Possibly place a cap on how much entropy may be read at a
// time.

#if defined(ABSL_RANDOM_USE_BCRYPT)

// On Windows potentially use the BCRYPT CNG API to read available entropy.
bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  BCRYPT_ALG_HANDLE hProvider;
  NTSTATUS ret;
  ret = BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_RNG_ALGORITHM,
                                    MS_PRIMITIVE_PROVIDER, 0);
  if (!(BCRYPT_SUCCESS(ret))) {
    ABSL_RAW_LOG(ERROR, "Failed to open crypto provider.");
    return false;
  }
  ret = BCryptGenRandom(
      hProvider,                                             // provider
      reinterpret_cast<UCHAR*>(values.data()),               // buffer
      static_cast<ULONG>(sizeof(uint32_t) * values.size()),  // bytes
      0);                                                    // flags
  BCryptCloseAlgorithmProvider(hProvider, 0);
  return BCRYPT_SUCCESS(ret);
}

#elif defined(ABSL_RANDOM_USE_NACL_SECURE_RANDOM)

// On NaCL use nacl_secure_random to acquire bytes.
bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  auto buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();

  uint8_t* output_ptr = buffer;
  while (buffer_size > 0) {
    size_t nread = 0;
    const int error = nacl_secure_random(output_ptr, buffer_size, &nread);
    if (error != 0 || nread > buffer_size) {
      ABSL_RAW_LOG(ERROR, "Failed to read secure_random seed data: %d", error);
      return false;
    }
    output_ptr += nread;
    buffer_size -= nread;
  }
  return true;
}

#elif defined(__Fuchsia__)

bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
  auto buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  zx_cprng_draw(buffer, buffer_size);
  return true;
}

#else

#if defined(ABSL_RANDOM_USE_GET_ENTROPY)
// On *nix, use getentropy() if supported. Note that libc may support
// getentropy(), but the kernel may not, in which case this function will return
// false.
bool ReadSeedMaterialFromGetEntropy(absl::Span<uint32_t> values) {
  auto buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();
  while (buffer_size > 0) {
    // getentropy() has a maximum permitted length of 256.
    size_t to_read = std::min<size_t>(buffer_size, 256);
    int result = getentropy(buffer, to_read);
    if (result < 0) {
      return false;
    }
    // https://github.com/google/sanitizers/issues/1173
    // MemorySanitizer can't see through getentropy().
    ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(buffer, to_read);
    buffer += to_read;
    buffer_size -= to_read;
  }
  return true;
}
#endif  // defined(ABSL_RANDOM_GETENTROPY)

// On *nix, read entropy from /dev/urandom.
bool ReadSeedMaterialFromDevURandom(absl::Span<uint32_t> values) {
  const char kEntropyFile[] = "/dev/urandom";

  auto buffer = reinterpret_cast<uint8_t*>(values.data());
  size_t buffer_size = sizeof(uint32_t) * values.size();

  int dev_urandom = open(kEntropyFile, O_RDONLY);
  bool success = (-1 != dev_urandom);
  if (!success) {
    return false;
  }

  while (success && buffer_size > 0) {
    ssize_t bytes_read = read(dev_urandom, buffer, buffer_size);
    int read_error = errno;
    success = (bytes_read > 0);
    if (success) {
      buffer += bytes_read;
      buffer_size -= static_cast<size_t>(bytes_read);
    } else if (bytes_read == -1 && read_error == EINTR) {
      success = true;  // Need to try again.
    }
  }
  close(dev_urandom);
  return success;
}

bool ReadSeedMaterialFromOSEntropyImpl(absl::Span<uint32_t> values) {
#if defined(ABSL_RANDOM_USE_GET_ENTROPY)
  if (ReadSeedMaterialFromGetEntropy(values)) {
    return true;
  }
#endif
  // Libc may support getentropy, but the kernel may not, so we still have
  // to fallback to ReadSeedMaterialFromDevURandom().
  return ReadSeedMaterialFromDevURandom(values);
}

#endif

}  // namespace

bool ReadSeedMaterialFromOSEntropy(absl::Span<uint32_t> values) {
  assert(values.data() != nullptr);
  if (values.data() == nullptr) {
    return false;
  }
  if (values.empty()) {
    return true;
  }
  return ReadSeedMaterialFromOSEntropyImpl(values);
}

void MixIntoSeedMaterial(absl::Span<const uint32_t> sequence,
                         absl::Span<uint32_t> seed_material) {
  // Algorithm is based on code available at
  // https://gist.github.com/imneme/540829265469e673d045
  constexpr uint32_t kInitVal = 0x43b0d7e5;
  constexpr uint32_t kHashMul = 0x931e8875;
  constexpr uint32_t kMixMulL = 0xca01f9dd;
  constexpr uint32_t kMixMulR = 0x4973f715;
  constexpr uint32_t kShiftSize = sizeof(uint32_t) * 8 / 2;

  uint32_t hash_const = kInitVal;
  auto hash = [&](uint32_t value) {
    value ^= hash_const;
    hash_const *= kHashMul;
    value *= hash_const;
    value ^= value >> kShiftSize;
    return value;
  };

  auto mix = [&](uint32_t x, uint32_t y) {
    uint32_t result = kMixMulL * x - kMixMulR * y;
    result ^= result >> kShiftSize;
    return result;
  };

  for (const auto& seq_val : sequence) {
    for (auto& elem : seed_material) {
      elem = mix(elem, hash(seq_val));
    }
  }
}

absl::optional<uint32_t> GetSaltMaterial() {
  // Salt must be common for all generators within the same process so read it
  // only once and store in static variable.
  static const auto salt_material = []() -> absl::optional<uint32_t> {
    uint32_t salt_value = 0;

    if (random_internal::ReadSeedMaterialFromOSEntropy(
            MakeSpan(&salt_value, 1))) {
      return salt_value;
    }

    return absl::nullopt;
  }();

  return salt_material;
}

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
