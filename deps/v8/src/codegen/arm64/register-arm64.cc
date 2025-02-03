// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/codegen/arm64/register-arm64.h"

namespace v8 {
namespace internal {

VectorFormat VectorFormatHalfWidth(VectorFormat vform) {
  DCHECK(vform == kFormat8H || vform == kFormat4S || vform == kFormat2D ||
         vform == kFormat1Q || vform == kFormatH || vform == kFormatS ||
         vform == kFormatD);
  switch (vform) {
    case kFormat8H:
      return kFormat8B;
    case kFormat4S:
      return kFormat4H;
    case kFormat2D:
      return kFormat2S;
    case kFormat1Q:
      return kFormat1D;
    case kFormatH:
      return kFormatB;
    case kFormatS:
      return kFormatH;
    case kFormatD:
      return kFormatS;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatDoubleWidth(VectorFormat vform) {
  DCHECK(vform == kFormat8B || vform == kFormat4H || vform == kFormat2S ||
         vform == kFormatB || vform == kFormatH || vform == kFormatS);
  switch (vform) {
    case kFormat8B:
      return kFormat8H;
    case kFormat4H:
      return kFormat4S;
    case kFormat2S:
      return kFormat2D;
    case kFormatB:
      return kFormatH;
    case kFormatH:
      return kFormatS;
    case kFormatS:
      return kFormatD;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatFillQ(VectorFormat vform) {
  switch (vform) {
    case kFormatB:
    case kFormat8B:
    case kFormat16B:
      return kFormat16B;
    case kFormatH:
    case kFormat4H:
    case kFormat8H:
      return kFormat8H;
    case kFormatS:
    case kFormat2S:
    case kFormat4S:
      return kFormat4S;
    case kFormatD:
    case kFormat1D:
    case kFormat2D:
      return kFormat2D;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatHalfWidthDoubleLanes(VectorFormat vform) {
  switch (vform) {
    case kFormat4H:
      return kFormat8B;
    case kFormat8H:
      return kFormat16B;
    case kFormat2S:
      return kFormat4H;
    case kFormat4S:
      return kFormat8H;
    case kFormat1D:
      return kFormat2S;
    case kFormat2D:
      return kFormat4S;
    case kFormat1Q:
      return kFormat2D;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatDoubleLanes(VectorFormat vform) {
  DCHECK(vform == kFormat8B || vform == kFormat4H || vform == kFormat2S);
  switch (vform) {
    case kFormat8B:
      return kFormat16B;
    case kFormat4H:
      return kFormat8H;
    case kFormat2S:
      return kFormat4S;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatHalfLanes(VectorFormat vform) {
  DCHECK(vform == kFormat16B || vform == kFormat8H || vform == kFormat4S);
  switch (vform) {
    case kFormat16B:
      return kFormat8B;
    case kFormat8H:
      return kFormat4H;
    case kFormat4S:
      return kFormat2S;
    default:
      UNREACHABLE();
  }
}

VectorFormat ScalarFormatFromLaneSize(int laneSize) {
  switch (laneSize) {
    case 8:
      return kFormatB;
    case 16:
      return kFormatH;
    case 32:
      return kFormatS;
    case 64:
      return kFormatD;
    default:
      UNREACHABLE();
  }
}

VectorFormat VectorFormatFillQ(int laneSize) {
  return VectorFormatFillQ(ScalarFormatFromLaneSize(laneSize));
}

VectorFormat ScalarFormatFromFormat(VectorFormat vform) {
  return ScalarFormatFromLaneSize(LaneSizeInBitsFromFormat(vform));
}

unsigned RegisterSizeInBytesFromFormat(VectorFormat vform) {
  return RegisterSizeInBitsFromFormat(vform) / 8;
}

unsigned RegisterSizeInBitsFromFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormatB:
      return kBRegSizeInBits;
    case kFormatH:
      return kHRegSizeInBits;
    case kFormatS:
      return kSRegSizeInBits;
    case kFormatD:
      return kDRegSizeInBits;
    case kFormat8B:
    case kFormat4H:
    case kFormat2S:
    case kFormat1D:
      return kDRegSizeInBits;
    default:
      return kQRegSizeInBits;
  }
}

unsigned LaneSizeInBitsFromFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormatB:
    case kFormat8B:
    case kFormat16B:
      return 8;
    case kFormatH:
    case kFormat4H:
    case kFormat8H:
      return 16;
    case kFormatS:
    case kFormat2S:
    case kFormat4S:
      return 32;
    case kFormatD:
    case kFormat1D:
    case kFormat2D:
      return 64;
    case kFormat1Q:
      return 128;
    default:
      UNREACHABLE();
  }
}

int LaneSizeInBytesFromFormat(VectorFormat vform) {
  return LaneSizeInBitsFromFormat(vform) / 8;
}

int LaneSizeInBytesLog2FromFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormatB:
    case kFormat8B:
    case kFormat16B:
      return 0;
    case kFormatH:
    case kFormat4H:
    case kFormat8H:
      return 1;
    case kFormatS:
    case kFormat2S:
    case kFormat4S:
      return 2;
    case kFormatD:
    case kFormat1D:
    case kFormat2D:
      return 3;
    default:
      UNREACHABLE();
  }
}

int LaneCountFromFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormat16B:
      return 16;
    case kFormat8B:
    case kFormat8H:
      return 8;
    case kFormat4H:
    case kFormat4S:
      return 4;
    case kFormat2S:
    case kFormat2D:
      return 2;
    case kFormat1D:
    case kFormat1Q:
    case kFormatB:
    case kFormatH:
    case kFormatS:
    case kFormatD:
      return 1;
    default:
      UNREACHABLE();
  }
}

int MaxLaneCountFromFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormatB:
    case kFormat8B:
    case kFormat16B:
      return 16;
    case kFormatH:
    case kFormat4H:
    case kFormat8H:
      return 8;
    case kFormatS:
    case kFormat2S:
    case kFormat4S:
      return 4;
    case kFormatD:
    case kFormat1D:
    case kFormat2D:
      return 2;
    default:
      UNREACHABLE();
  }
}

// Does 'vform' indicate a vector format or a scalar format?
bool IsVectorFormat(VectorFormat vform) {
  DCHECK_NE(vform, kFormatUndefined);
  switch (vform) {
    case kFormatB:
    case kFormatH:
    case kFormatS:
    case kFormatD:
      return false;
    default:
      return true;
  }
}

int64_t MaxIntFromFormat(VectorFormat vform) {
  return INT64_MAX >> (64 - LaneSizeInBitsFromFormat(vform));
}

int64_t MinIntFromFormat(VectorFormat vform) {
  return INT64_MIN >> (64 - LaneSizeInBitsFromFormat(vform));
}

uint64_t MaxUintFromFormat(VectorFormat vform) {
  return UINT64_MAX >> (64 - LaneSizeInBitsFromFormat(vform));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
