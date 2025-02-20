// Copyright 2020 Google LLC
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

#include "hwy/contrib/image/image.h"

#include <algorithm>  // std::swap
#include <cstddef>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/image/image.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
size_t GetVectorSize() { return Lanes(ScalableTag<uint8_t>()); }
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE

}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_EXPORT(GetVectorSize);  // Local function.
}  // namespace

size_t ImageBase::VectorSize() {
  // Do not cache result - must return the current value, which may be greater
  // than the first call if it was subject to DisableTargets!
  return HWY_DYNAMIC_DISPATCH(GetVectorSize)();
}

size_t ImageBase::BytesPerRow(const size_t xsize, const size_t sizeof_t) {
  const size_t vec_size = VectorSize();
  size_t valid_bytes = xsize * sizeof_t;

  // Allow unaligned accesses starting at the last valid value - this may raise
  // msan errors unless the user calls InitializePaddingForUnalignedAccesses.
  // Skip for the scalar case because no extra lanes will be loaded.
  if (vec_size != 1) {
    HWY_DASSERT(vec_size >= sizeof_t);
    valid_bytes += vec_size - sizeof_t;
  }

  // Round up to vector and cache line size.
  const size_t align = HWY_MAX(vec_size, HWY_ALIGNMENT);
  size_t bytes_per_row = RoundUpTo(valid_bytes, align);

  // During the lengthy window before writes are committed to memory, CPUs
  // guard against read after write hazards by checking the address, but
  // only the lower 11 bits. We avoid a false dependency between writes to
  // consecutive rows by ensuring their sizes are not multiples of 2 KiB.
  // Avoid2K prevents the same problem for the planes of an Image3.
  if (bytes_per_row % HWY_ALIGNMENT == 0) {
    bytes_per_row += align;
  }

  HWY_DASSERT(bytes_per_row % align == 0);
  return bytes_per_row;
}

ImageBase::ImageBase(const size_t xsize, const size_t ysize,
                     const size_t sizeof_t)
    : xsize_(static_cast<uint32_t>(xsize)),
      ysize_(static_cast<uint32_t>(ysize)),
      bytes_(nullptr, AlignedFreer(&AlignedFreer::DoNothing, nullptr)) {
  HWY_ASSERT(sizeof_t == 1 || sizeof_t == 2 || sizeof_t == 4 || sizeof_t == 8);

  bytes_per_row_ = 0;
  // Dimensions can be zero, e.g. for lazily-allocated images. Only allocate
  // if nonzero, because "zero" bytes still have padding/bookkeeping overhead.
  if (xsize != 0 && ysize != 0) {
    bytes_per_row_ = BytesPerRow(xsize, sizeof_t);
    bytes_ = AllocateAligned<uint8_t>(bytes_per_row_ * ysize);
    HWY_ASSERT(bytes_.get() != nullptr);
    InitializePadding(sizeof_t, Padding::kRoundUp);
  }
}

ImageBase::ImageBase(const size_t xsize, const size_t ysize,
                     const size_t bytes_per_row, void* const aligned)
    : xsize_(static_cast<uint32_t>(xsize)),
      ysize_(static_cast<uint32_t>(ysize)),
      bytes_per_row_(bytes_per_row),
      bytes_(static_cast<uint8_t*>(aligned),
             AlignedFreer(&AlignedFreer::DoNothing, nullptr)) {
  const size_t vec_size = VectorSize();
  HWY_ASSERT(bytes_per_row % vec_size == 0);
  HWY_ASSERT(reinterpret_cast<uintptr_t>(aligned) % vec_size == 0);
}

void ImageBase::InitializePadding(const size_t sizeof_t, Padding padding) {
#if HWY_IS_MSAN || HWY_IDE
  if (xsize_ == 0 || ysize_ == 0) return;

  const size_t vec_size = VectorSize();  // Bytes, independent of sizeof_t!
  if (vec_size == 1) return;             // Scalar mode: no padding needed

  const size_t valid_size = xsize_ * sizeof_t;
  const size_t initialize_size = padding == Padding::kRoundUp
                                     ? RoundUpTo(valid_size, vec_size)
                                     : valid_size + vec_size - sizeof_t;
  if (valid_size == initialize_size) return;

  for (size_t y = 0; y < ysize_; ++y) {
    uint8_t* HWY_RESTRICT row = static_cast<uint8_t*>(VoidRow(y));
#if defined(__clang__) && (__clang_major__ <= 6)
    // There's a bug in msan in clang-6 when handling AVX2 operations. This
    // workaround allows tests to pass on msan, although it is slower and
    // prevents msan warnings from uninitialized images.
    memset(row, 0, initialize_size);
#else
    memset(row + valid_size, 0, initialize_size - valid_size);
#endif  // clang6
  }
#else
  (void)sizeof_t;
  (void)padding;
#endif  // HWY_IS_MSAN
}

void ImageBase::Swap(ImageBase& other) {
  std::swap(xsize_, other.xsize_);
  std::swap(ysize_, other.ysize_);
  std::swap(bytes_per_row_, other.bytes_per_row_);
  std::swap(bytes_, other.bytes_);
}

}  // namespace hwy
#endif  // HWY_ONCE
