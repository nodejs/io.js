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

#ifndef HIGHWAY_HWY_CONTRIB_IMAGE_IMAGE_H_
#define HIGHWAY_HWY_CONTRIB_IMAGE_IMAGE_H_

// SIMD/multicore-friendly planar image representation with row accessors.

#include <string.h>

#include <utility>  // std::move

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"

namespace hwy {

// Type-independent parts of Image<> - reduces code duplication and facilitates
// moving member function implementations to cc file.
struct HWY_CONTRIB_DLLEXPORT ImageBase {
  // Returns required alignment in bytes for externally allocated memory.
  static size_t VectorSize();

  // Returns distance [bytes] between the start of two consecutive rows, a
  // multiple of VectorSize but NOT kAlias (see implementation).
  static size_t BytesPerRow(size_t xsize, size_t sizeof_t);

  // No allocation (for output params or unused images)
  ImageBase()
      : xsize_(0),
        ysize_(0),
        bytes_per_row_(0),
        bytes_(nullptr, AlignedFreer(&AlignedFreer::DoNothing, nullptr)) {}

  // Allocates memory (this is the common case)
  ImageBase(size_t xsize, size_t ysize, size_t sizeof_t);

  // References but does not take ownership of external memory. Useful for
  // interoperability with other libraries. `aligned` must be aligned to a
  // multiple of VectorSize() and `bytes_per_row` must also be a multiple of
  // VectorSize() or preferably equal to BytesPerRow().
  ImageBase(size_t xsize, size_t ysize, size_t bytes_per_row, void* aligned);

  // Copy construction/assignment is forbidden to avoid inadvertent copies,
  // which can be very expensive. Use CopyImageTo() instead.
  ImageBase(const ImageBase& other) = delete;
  ImageBase& operator=(const ImageBase& other) = delete;

  // Move constructor (required for returning Image from function)
  ImageBase(ImageBase&& other) noexcept = default;

  // Move assignment (required for std::vector)
  ImageBase& operator=(ImageBase&& other) noexcept = default;

  void Swap(ImageBase& other);

  // Useful for pre-allocating image with some padding for alignment purposes
  // and later reporting the actual valid dimensions. Caller is responsible
  // for ensuring xsize/ysize are <= the original dimensions.
  void ShrinkTo(const size_t xsize, const size_t ysize) {
    xsize_ = static_cast<uint32_t>(xsize);
    ysize_ = static_cast<uint32_t>(ysize);
    // NOTE: we can't recompute bytes_per_row for more compact storage and
    // better locality because that would invalidate the image contents.
  }

  // How many pixels.
  HWY_INLINE size_t xsize() const { return xsize_; }
  HWY_INLINE size_t ysize() const { return ysize_; }

  // NOTE: do not use this for copying rows - the valid xsize may be much less.
  HWY_INLINE size_t bytes_per_row() const { return bytes_per_row_; }

  // Raw access to byte contents, for interfacing with other libraries.
  // Unsigned char instead of char to avoid surprises (sign extension).
  HWY_INLINE uint8_t* bytes() {
    void* p = bytes_.get();
    return static_cast<uint8_t * HWY_RESTRICT>(HWY_ASSUME_ALIGNED(p, 64));
  }
  HWY_INLINE const uint8_t* bytes() const {
    const void* p = bytes_.get();
    return static_cast<const uint8_t * HWY_RESTRICT>(HWY_ASSUME_ALIGNED(p, 64));
  }

 protected:
  // Returns pointer to the start of a row.
  HWY_INLINE void* VoidRow(const size_t y) const {
#if HWY_IS_ASAN || HWY_IS_MSAN || HWY_IS_TSAN
    if (y >= ysize_) {
      HWY_ABORT("Row(%d) >= %u\n", static_cast<int>(y), ysize_);
    }
#endif

    void* row = bytes_.get() + y * bytes_per_row_;
    return HWY_ASSUME_ALIGNED(row, 64);
  }

  enum class Padding {
    // Allow Load(d, row + x) for x = 0; x < xsize(); x += Lanes(d). Default.
    kRoundUp,
    // Allow LoadU(d, row + x) for x <= xsize() - 1. This requires an extra
    // vector to be initialized. If done by default, this would suppress
    // legitimate msan warnings. We therefore require users to explicitly call
    // InitializePadding before using unaligned loads (e.g. convolution).
    kUnaligned
  };

  // Initializes the minimum bytes required to suppress msan warnings from
  // legitimate (according to Padding mode) vector loads/stores on the right
  // border, where some lanes are uninitialized and assumed to be unused.
  void InitializePadding(size_t sizeof_t, Padding padding);

  // (Members are non-const to enable assignment during move-assignment.)
  uint32_t xsize_;  // In valid pixels, not including any padding.
  uint32_t ysize_;
  size_t bytes_per_row_;  // Includes padding.
  AlignedFreeUniquePtr<uint8_t[]> bytes_;
};

// Single channel, aligned rows separated by padding. T must be POD.
//
// 'Single channel' (one 2D array per channel) simplifies vectorization
// (repeating the same operation on multiple adjacent components) without the
// complexity of a hybrid layout (8 R, 8 G, 8 B, ...). In particular, clients
// can easily iterate over all components in a row and Image requires no
// knowledge of the pixel format beyond the component type "T".
//
// 'Aligned' means each row is aligned to the L1 cache line size. This prevents
// false sharing between two threads operating on adjacent rows.
//
// 'Padding' is still relevant because vectors could potentially be larger than
// a cache line. By rounding up row sizes to the vector size, we allow
// reading/writing ALIGNED vectors whose first lane is a valid sample. This
// avoids needing a separate loop to handle remaining unaligned lanes.
//
// This image layout could also be achieved with a vector and a row accessor
// function, but a class wrapper with support for "deleter" allows wrapping
// existing memory allocated by clients without copying the pixels. It also
// provides convenient accessors for xsize/ysize, which shortens function
// argument lists. Supports move-construction so it can be stored in containers.
template <typename ComponentType>
class Image : public ImageBase {
 public:
  using T = ComponentType;

  Image() = default;
  Image(const size_t xsize, const size_t ysize)
      : ImageBase(xsize, ysize, sizeof(T)) {}
  Image(const size_t xsize, const size_t ysize, size_t bytes_per_row,
        void* aligned)
      : ImageBase(xsize, ysize, bytes_per_row, aligned) {}

  void InitializePaddingForUnalignedAccesses() {
    InitializePadding(sizeof(T), Padding::kUnaligned);
  }

  HWY_INLINE const T* ConstRow(const size_t y) const {
    return static_cast<const T*>(VoidRow(y));
  }
  HWY_INLINE const T* ConstRow(const size_t y) {
    return static_cast<const T*>(VoidRow(y));
  }

  // Returns pointer to non-const. This allows passing const Image* parameters
  // when the callee is only supposed to fill the pixels, as opposed to
  // allocating or resizing the image.
  HWY_INLINE T* MutableRow(const size_t y) const {
    return static_cast<T*>(VoidRow(y));
  }
  HWY_INLINE T* MutableRow(const size_t y) {
    return static_cast<T*>(VoidRow(y));
  }

  // Returns number of pixels (some of which are padding) per row. Useful for
  // computing other rows via pointer arithmetic. WARNING: this must
  // NOT be used to determine xsize.
  HWY_INLINE intptr_t PixelsPerRow() const {
    return static_cast<intptr_t>(bytes_per_row_ / sizeof(T));
  }
};

using ImageF = Image<float>;

// A bundle of 3 same-sized images. To fill an existing Image3 using
// single-channel producers, we also need access to each const Image*. Const
// prevents breaking the same-size invariant, while still allowing pixels to be
// changed via MutableRow.
template <typename ComponentType>
class Image3 {
 public:
  using T = ComponentType;
  using ImageT = Image<T>;
  static constexpr size_t kNumPlanes = 3;

  Image3() : planes_{ImageT(), ImageT(), ImageT()} {}

  Image3(const size_t xsize, const size_t ysize)
      : planes_{ImageT(xsize, ysize), ImageT(xsize, ysize),
                ImageT(xsize, ysize)} {}

  Image3(Image3&& other) noexcept {
    for (size_t i = 0; i < kNumPlanes; i++) {
      planes_[i] = std::move(other.planes_[i]);
    }
  }

  Image3(ImageT&& plane0, ImageT&& plane1, ImageT&& plane2) {
    if (!SameSize(plane0, plane1) || !SameSize(plane0, plane2)) {
      HWY_ABORT(
          "Not same size: %d x %d, %d x %d, %d x %d\n",
          static_cast<int>(plane0.xsize()), static_cast<int>(plane0.ysize()),
          static_cast<int>(plane1.xsize()), static_cast<int>(plane1.ysize()),
          static_cast<int>(plane2.xsize()), static_cast<int>(plane2.ysize()));
    }
    planes_[0] = std::move(plane0);
    planes_[1] = std::move(plane1);
    planes_[2] = std::move(plane2);
  }

  // Copy construction/assignment is forbidden to avoid inadvertent copies,
  // which can be very expensive. Use CopyImageTo instead.
  Image3(const Image3& other) = delete;
  Image3& operator=(const Image3& other) = delete;

  Image3& operator=(Image3&& other) noexcept {
    for (size_t i = 0; i < kNumPlanes; i++) {
      planes_[i] = std::move(other.planes_[i]);
    }
    return *this;
  }

  HWY_INLINE const T* ConstPlaneRow(const size_t c, const size_t y) const {
    return static_cast<const T*>(VoidPlaneRow(c, y));
  }
  HWY_INLINE const T* ConstPlaneRow(const size_t c, const size_t y) {
    return static_cast<const T*>(VoidPlaneRow(c, y));
  }

  HWY_INLINE T* MutablePlaneRow(const size_t c, const size_t y) const {
    return static_cast<T*>(VoidPlaneRow(c, y));
  }
  HWY_INLINE T* MutablePlaneRow(const size_t c, const size_t y) {
    return static_cast<T*>(VoidPlaneRow(c, y));
  }

  HWY_INLINE const ImageT& Plane(size_t idx) const { return planes_[idx]; }

  void Swap(Image3& other) {
    for (size_t c = 0; c < 3; ++c) {
      other.planes_[c].Swap(planes_[c]);
    }
  }

  void ShrinkTo(const size_t xsize, const size_t ysize) {
    for (ImageT& plane : planes_) {
      plane.ShrinkTo(xsize, ysize);
    }
  }

  // Sizes of all three images are guaranteed to be equal.
  HWY_INLINE size_t xsize() const { return planes_[0].xsize(); }
  HWY_INLINE size_t ysize() const { return planes_[0].ysize(); }
  // Returns offset [bytes] from one row to the next row of the same plane.
  // WARNING: this must NOT be used to determine xsize, nor for copying rows -
  // the valid xsize may be much less.
  HWY_INLINE size_t bytes_per_row() const { return planes_[0].bytes_per_row(); }
  // Returns number of pixels (some of which are padding) per row. Useful for
  // computing other rows via pointer arithmetic. WARNING: this must NOT be used
  // to determine xsize.
  HWY_INLINE intptr_t PixelsPerRow() const { return planes_[0].PixelsPerRow(); }

 private:
  // Returns pointer to the start of a row.
  HWY_INLINE void* VoidPlaneRow(const size_t c, const size_t y) const {
#if HWY_IS_ASAN || HWY_IS_MSAN || HWY_IS_TSAN
    if (c >= kNumPlanes || y >= ysize()) {
      HWY_ABORT("PlaneRow(%d, %d) >= %d\n", static_cast<int>(c),
                static_cast<int>(y), static_cast<int>(ysize()));
    }
#endif
    // Use the first plane's stride because the compiler might not realize they
    // are all equal. Thus we only need a single multiplication for all planes.
    const size_t row_offset = y * planes_[0].bytes_per_row();
    const void* row = planes_[c].bytes() + row_offset;
    return static_cast<const T * HWY_RESTRICT>(
        HWY_ASSUME_ALIGNED(row, HWY_ALIGNMENT));
  }

 private:
  ImageT planes_[kNumPlanes];
};

using Image3F = Image3<float>;

// Rectangular region in image(s). Factoring this out of Image instead of
// shifting the pointer by x0/y0 allows this to apply to multiple images with
// different resolutions. Can compare size via SameSize(rect1, rect2).
class Rect {
 public:
  // Most windows are xsize_max * ysize_max, except those on the borders where
  // begin + size_max > end.
  constexpr Rect(size_t xbegin, size_t ybegin, size_t xsize_max,
                 size_t ysize_max, size_t xend, size_t yend)
      : x0_(xbegin),
        y0_(ybegin),
        xsize_(ClampedSize(xbegin, xsize_max, xend)),
        ysize_(ClampedSize(ybegin, ysize_max, yend)) {}

  // Construct with origin and known size (typically from another Rect).
  constexpr Rect(size_t xbegin, size_t ybegin, size_t xsize, size_t ysize)
      : x0_(xbegin), y0_(ybegin), xsize_(xsize), ysize_(ysize) {}

  // Construct a rect that covers a whole image.
  template <typename Image>
  explicit Rect(const Image& image)
      : Rect(0, 0, image.xsize(), image.ysize()) {}

  Rect() : Rect(0, 0, 0, 0) {}

  Rect(const Rect&) = default;
  Rect& operator=(const Rect&) = default;

  Rect Subrect(size_t xbegin, size_t ybegin, size_t xsize_max,
               size_t ysize_max) {
    return Rect(x0_ + xbegin, y0_ + ybegin, xsize_max, ysize_max, x0_ + xsize_,
                y0_ + ysize_);
  }

  template <typename T>
  const T* ConstRow(const Image<T>* image, size_t y) const {
    return image->ConstRow(y + y0_) + x0_;
  }

  template <typename T>
  T* MutableRow(const Image<T>* image, size_t y) const {
    return image->MutableRow(y + y0_) + x0_;
  }

  template <typename T>
  const T* ConstPlaneRow(const Image3<T>& image, size_t c, size_t y) const {
    return image.ConstPlaneRow(c, y + y0_) + x0_;
  }

  template <typename T>
  T* MutablePlaneRow(Image3<T>* image, const size_t c, size_t y) const {
    return image->MutablePlaneRow(c, y + y0_) + x0_;
  }

  // Returns true if this Rect fully resides in the given image. ImageT could be
  // Image<T> or Image3<T>; however if ImageT is Rect, results are nonsensical.
  template <class ImageT>
  bool IsInside(const ImageT& image) const {
    return (x0_ + xsize_ <= image.xsize()) && (y0_ + ysize_ <= image.ysize());
  }

  size_t x0() const { return x0_; }
  size_t y0() const { return y0_; }
  size_t xsize() const { return xsize_; }
  size_t ysize() const { return ysize_; }

 private:
  // Returns size_max, or whatever is left in [begin, end).
  static constexpr size_t ClampedSize(size_t begin, size_t size_max,
                                      size_t end) {
    return (begin + size_max <= end) ? size_max
                                     : (end > begin ? end - begin : 0);
  }

  size_t x0_;
  size_t y0_;

  size_t xsize_;
  size_t ysize_;
};

// Works for any image-like input type(s).
template <class Image1, class Image2>
HWY_MAYBE_UNUSED bool SameSize(const Image1& image1, const Image2& image2) {
  return image1.xsize() == image2.xsize() && image1.ysize() == image2.ysize();
}

// Mirrors out of bounds coordinates and returns valid coordinates unchanged.
// We assume the radius (distance outside the image) is small compared to the
// image size, otherwise this might not terminate.
// The mirror is outside the last column (border pixel is also replicated).
static HWY_INLINE HWY_MAYBE_UNUSED size_t Mirror(int64_t x,
                                                 const int64_t xsize) {
  HWY_DASSERT(xsize != 0);

  // TODO(janwas): replace with branchless version
  while (x < 0 || x >= xsize) {
    if (x < 0) {
      x = -x - 1;
    } else {
      x = 2 * xsize - 1 - x;
    }
  }
  return static_cast<size_t>(x);
}

// Wrap modes for ensuring X/Y coordinates are in the valid range [0, size):

// Mirrors (repeating the edge pixel once). Useful for convolutions.
struct WrapMirror {
  HWY_INLINE size_t operator()(const int64_t coord, const size_t size) const {
    return Mirror(coord, static_cast<int64_t>(size));
  }
};

// Returns the same coordinate, for when we know "coord" is already valid (e.g.
// interior of an image).
struct WrapUnchanged {
  HWY_INLINE size_t operator()(const int64_t coord, size_t /*size*/) const {
    return static_cast<size_t>(coord);
  }
};

// Similar to Wrap* but for row pointers (reduces Row() multiplications).

class WrapRowMirror {
 public:
  template <class View>
  WrapRowMirror(const View& image, size_t ysize)
      : first_row_(image.ConstRow(0)), last_row_(image.ConstRow(ysize - 1)) {}

  const float* operator()(const float* const HWY_RESTRICT row,
                          const int64_t stride) const {
    if (row < first_row_) {
      const int64_t num_before = first_row_ - row;
      // Mirrored; one row before => row 0, two before = row 1, ...
      return first_row_ + num_before - stride;
    }
    if (row > last_row_) {
      const int64_t num_after = row - last_row_;
      // Mirrored; one row after => last row, two after = last - 1, ...
      return last_row_ - num_after + stride;
    }
    return row;
  }

 private:
  const float* const HWY_RESTRICT first_row_;
  const float* const HWY_RESTRICT last_row_;
};

struct WrapRowUnchanged {
  HWY_INLINE const float* operator()(const float* const HWY_RESTRICT row,
                                     int64_t /*stride*/) const {
    return row;
  }
};

}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_IMAGE_IMAGE_H_
