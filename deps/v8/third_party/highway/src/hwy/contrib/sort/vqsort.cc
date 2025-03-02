// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/contrib/sort/vqsort.h"

#include "hwy/base.h"
#include "hwy/contrib/sort/vqsort-inl.h"
#include "hwy/per_target.h"

// Check if we have getrandom from <sys/random.h>. Because <features.h> is
// unavailable on Android and non-Linux RVV, we assume that those systems lack
// getrandom. Note that the only supported sources of entropy are getrandom or
// Windows, thus VQSORT_SECURE_SEED=0 when this is 0 and we are not on Windows.
#if defined(ANDROID) || defined(__ANDROID__) || \
    (HWY_ARCH_RISCV && !HWY_OS_LINUX)
#define VQSORT_GETRANDOM 0
#endif

#if !defined(VQSORT_GETRANDOM) && HWY_OS_LINUX
#include <features.h>

// ---- which libc
#if defined(__UCLIBC__)
#define VQSORT_GETRANDOM 1  // added Mar 2015, before uclibc-ng 1.0

#elif defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 25)
#define VQSORT_GETRANDOM 1
#else
#define VQSORT_GETRANDOM 0
#endif

#else
// Assume MUSL, which has getrandom since 2018. There is no macro to test, see
// https://www.openwall.com/lists/musl/2013/03/29/13.
#define VQSORT_GETRANDOM 1

#endif  // ---- which libc
#endif  // linux

#if !defined(VQSORT_GETRANDOM)
#define VQSORT_GETRANDOM 0
#endif

// Choose a seed source for SFC generator: 1=getrandom, 2=CryptGenRandom.
// Allow user override - not all Android support the getrandom wrapper.
#ifndef VQSORT_SECURE_SEED

#if VQSORT_GETRANDOM
#define VQSORT_SECURE_SEED 1
#elif defined(_WIN32) || defined(_WIN64)
#define VQSORT_SECURE_SEED 2
#else
#define VQSORT_SECURE_SEED 0
#endif

#endif  // VQSORT_SECURE_SEED

// Pull in dependencies of the chosen seed source.
#if VQSORT_SECURE_SEED == 1
#include <sys/random.h>
#elif VQSORT_SECURE_SEED == 2
#include <windows.h>
#if HWY_COMPILER_MSVC || HWY_COMPILER_CLANGCL
#pragma comment(lib, "advapi32.lib")
#endif  // HWY_COMPILER_MSVC || HWY_COMPILER_CLANGCL
// Must come after windows.h.
#include <wincrypt.h>
#endif  // VQSORT_SECURE_SEED

namespace hwy {

// Returns false or performs the equivalent of `memcpy(bytes, r, 16)`, where r
// is high-quality (unpredictable, uniformly distributed) random bits.
bool Fill16BytesSecure(void* bytes) {
#if VQSORT_SECURE_SEED == 1
  // May block if urandom is not yet initialized.
  const ssize_t ret = getrandom(bytes, 16, /*flags=*/0);
  if (ret == 16) return true;
#elif VQSORT_SECURE_SEED == 2
  HCRYPTPROV hProvider{};
  if (CryptAcquireContextA(&hProvider, nullptr, nullptr, PROV_RSA_FULL,
                           CRYPT_VERIFYCONTEXT)) {
    const BOOL ok =
        CryptGenRandom(hProvider, 16, reinterpret_cast<BYTE*>(bytes));
    CryptReleaseContext(hProvider, 0);
    if (ok) return true;
  }
#else
  (void)bytes;
#endif

  return false;
}

void Sorter::operator()(uint16_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint16_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint32_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint32_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint64_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint64_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

void Sorter::operator()(int16_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(int16_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(int32_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(int32_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(int64_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(int64_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

void Sorter::operator()(float16_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(float16_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(float* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(float* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(double* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(double* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

void Sorter::operator()(uint128_t* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(uint128_t* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

void Sorter::operator()(K64V64* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(K64V64* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

void Sorter::operator()(K32V32* HWY_RESTRICT keys, size_t n,
                        SortAscending tag) const {
  VQSort(keys, n, tag);
}
void Sorter::operator()(K32V32* HWY_RESTRICT keys, size_t n,
                        SortDescending tag) const {
  VQSort(keys, n, tag);
}

// Unused, only for ABI compatibility
void Sorter::Fill24Bytes(const void*, size_t, void*) {}
bool Sorter::HaveFloat64() { return hwy::HaveFloat64(); }
Sorter::Sorter() {}
void Sorter::Delete() {}
uint64_t* GetGeneratorState() { return hwy::detail::GetGeneratorStateStatic(); }

}  // namespace hwy
