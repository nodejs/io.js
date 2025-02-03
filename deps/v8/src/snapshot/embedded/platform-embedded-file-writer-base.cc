// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-base.h"

#include <string>

#include "src/common/globals.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-aix.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-generic.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-mac.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-zos.h"

namespace v8 {
namespace internal {

DataDirective PointerSizeDirective() {
  if (kSystemPointerSize == 8) {
    return kQuad;
  } else {
    CHECK_EQ(4, kSystemPointerSize);
    return kLong;
  }
}

int PlatformEmbeddedFileWriterBase::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0x%" PRIx64, value);
}

int DataDirectiveSize(DataDirective directive) {
  switch (directive) {
    case kByte:
      return 1;
    case kLong:
      return 4;
    case kQuad:
      return 8;
    case kOcta:
      return 16;
  }
  UNREACHABLE();
}

int PlatformEmbeddedFileWriterBase::WriteByteChunk(const uint8_t* data) {
  size_t kSize = DataDirectiveSize(ByteChunkDataDirective());
  size_t kHalfSize = kSize / 2;
  uint64_t high = 0, low = 0;

  switch (kSize) {
    case 1:
      low = *data;
      break;
    case 4:
      low = *reinterpret_cast<const uint32_t*>(data);
      break;
    case 8:
      low = *reinterpret_cast<const uint64_t*>(data);
      break;
    case 16:
#ifdef V8_TARGET_BIG_ENDIAN
      memcpy(&high, data, kHalfSize);
      memcpy(&low, data + kHalfSize, kHalfSize);
#else
      memcpy(&high, data + kHalfSize, kHalfSize);
      memcpy(&low, data, kHalfSize);
#endif  // V8_TARGET_BIG_ENDIAN
      break;
    default:
      UNREACHABLE();
  }

  if (high != 0) {
    return fprintf(fp(), "0x%" PRIx64 "%016" PRIx64, high, low);
  } else {
    return fprintf(fp(), "0x%" PRIx64, low);
  }
}

namespace {

EmbeddedTargetArch DefaultEmbeddedTargetArch() {
#if defined(V8_TARGET_ARCH_ARM)
  return EmbeddedTargetArch::kArm;
#elif defined(V8_TARGET_ARCH_ARM64)
  return EmbeddedTargetArch::kArm64;
#elif defined(V8_TARGET_ARCH_IA32)
  return EmbeddedTargetArch::kIA32;
#elif defined(V8_TARGET_ARCH_X64)
  return EmbeddedTargetArch::kX64;
#else
  return EmbeddedTargetArch::kGeneric;
#endif
}

EmbeddedTargetArch ToEmbeddedTargetArch(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetArch();
  }

  std::string string(s);
  if (string == "arm") {
    return EmbeddedTargetArch::kArm;
  } else if (string == "arm64") {
    return EmbeddedTargetArch::kArm64;
  } else if (string == "ia32") {
    return EmbeddedTargetArch::kIA32;
  } else if (string == "x64") {
    return EmbeddedTargetArch::kX64;
  } else {
    return EmbeddedTargetArch::kGeneric;
  }
}

EmbeddedTargetOs DefaultEmbeddedTargetOs() {
#if defined(V8_OS_AIX)
  return EmbeddedTargetOs::kAIX;
#elif defined(V8_OS_DARWIN)
  return EmbeddedTargetOs::kMac;
#elif defined(V8_OS_WIN)
  return EmbeddedTargetOs::kWin;
#elif defined(V8_OS_ZOS)
  return EmbeddedTargetOs::kZOS;
#else
  return EmbeddedTargetOs::kGeneric;
#endif
}

EmbeddedTargetOs ToEmbeddedTargetOs(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetOs();
  }

  std::string string(s);
  // Python 3.9+ on IBM i returns os400 as sys.platform instead of aix
  if (string == "aix" || string == "os400") {
    return EmbeddedTargetOs::kAIX;
  } else if (string == "chromeos") {
    return EmbeddedTargetOs::kChromeOS;
  } else if (string == "fuchsia") {
    return EmbeddedTargetOs::kFuchsia;
  } else if (string == "ios" || string == "mac") {
    return EmbeddedTargetOs::kMac;
  } else if (string == "win") {
    return EmbeddedTargetOs::kWin;
  } else if (string == "starboard") {
    return EmbeddedTargetOs::kStarboard;
  } else if (string == "zos") {
    return EmbeddedTargetOs::kZOS;
  } else {
    return EmbeddedTargetOs::kGeneric;
  }
}

}  // namespace

std::unique_ptr<PlatformEmbeddedFileWriterBase> NewPlatformEmbeddedFileWriter(
    const char* target_arch, const char* target_os) {
  auto embedded_target_arch = ToEmbeddedTargetArch(target_arch);
  auto embedded_target_os = ToEmbeddedTargetOs(target_os);

  if (embedded_target_os == EmbeddedTargetOs::kStarboard) {
    // target OS is "Starboard" for all starboard build so we need to
    // use host OS macros to decide which writer to use.
    // Cobalt also has Windows-based Posix target platform,
    // in which case generic writer should be used.
    switch (DefaultEmbeddedTargetOs()) {
      case EmbeddedTargetOs::kMac:
#if defined(V8_TARGET_OS_WIN)
      case EmbeddedTargetOs::kWin:
        // V8_TARGET_OS_WIN is used to enable WINDOWS-specific assembly code,
        // for windows-hosted non-windows targets, we should still fallback to
        // the generic writer.
#endif
        embedded_target_os = DefaultEmbeddedTargetOs();
        break;
      default:
        // In the block below, we will use WriterGeneric for other cases.
        break;
    }
  }

  if (embedded_target_os == EmbeddedTargetOs::kAIX) {
    return std::make_unique<PlatformEmbeddedFileWriterAIX>(embedded_target_arch,
                                                           embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kMac) {
    return std::make_unique<PlatformEmbeddedFileWriterMac>(embedded_target_arch,
                                                           embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kWin) {
    return std::make_unique<PlatformEmbeddedFileWriterWin>(embedded_target_arch,
                                                           embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kZOS) {
    return std::make_unique<PlatformEmbeddedFileWriterZOS>(embedded_target_arch,
                                                           embedded_target_os);
  } else {
    return std::make_unique<PlatformEmbeddedFileWriterGeneric>(
        embedded_target_arch, embedded_target_os);
  }

  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
