// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_BASE_H_
#define V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_BASE_H_

#include <cinttypes>
#include <cstdio>  // For FILE.
#include <memory>

#if V8_ENABLE_DRUMBRAKE
#include <string>
#endif  // V8_ENABLE_DRUMBRAKE

namespace v8 {
namespace internal {

class EmbeddedData;

enum DataDirective {
  kByte,
  kLong,
  kQuad,
  kOcta,
};

DataDirective PointerSizeDirective();
int DataDirectiveSize(DataDirective directive);

enum class EmbeddedTargetOs {
  kAIX,
  kChromeOS,
  kFuchsia,
  kMac,
  kWin,
  kStarboard,
  kZOS,
  kGeneric,  // Everything not covered above falls in here.
};

enum class EmbeddedTargetArch {
  kArm,
  kArm64,
  kIA32,
  kX64,
  kGeneric,  // Everything not covered above falls in here.
};

// The platform-dependent logic for emitting assembly code for the generated
// embedded.S file.
class PlatformEmbeddedFileWriterBase {
 public:
  virtual ~PlatformEmbeddedFileWriterBase() = default;

  void SetFile(FILE* fp) { fp_ = fp; }
  FILE* fp() const { return fp_; }

  virtual void SectionText() = 0;
  virtual void SectionRoData() = 0;

  virtual void AlignToCodeAlignment() = 0;
  virtual void AlignToPageSizeIfNeeded() {}
  virtual void AlignToDataAlignment() = 0;

  virtual void DeclareUint32(const char* name, uint32_t value) = 0;

  virtual void DeclareSymbolGlobal(const char* name) = 0;
  virtual void DeclareLabel(const char* name) = 0;
  virtual void DeclareLabelProlog(const char* name) {}
  virtual void DeclareLabelEpilogue() {}

  virtual void SourceInfo(int fileid, const char* filename, int line) = 0;
  virtual void DeclareFunctionBegin(const char* name, uint32_t size) = 0;
  virtual void DeclareFunctionEnd(const char* name) = 0;

  // Returns the number of printed characters.
  virtual int HexLiteral(uint64_t value);

  virtual void Comment(const char* string) = 0;
  virtual void Newline() { fprintf(fp_, "\n"); }

  virtual void FilePrologue() = 0;
  virtual void DeclareExternalFilename(int fileid, const char* filename) = 0;
  virtual void FileEpilogue() = 0;

  virtual int IndentedDataDirective(DataDirective directive) = 0;

  virtual DataDirective ByteChunkDataDirective() const { return kOcta; }
  virtual int WriteByteChunk(const uint8_t* data);

  // This awkward interface works around the fact that unwind data emission
  // is both high-level and platform-dependent. The former implies it should
  // live in EmbeddedFileWriter, but code there should be platform-independent.
  //
  // Emits unwinding data on x64 Windows, and does nothing otherwise.
  virtual void MaybeEmitUnwindData(const char* unwind_info_symbol,
                                   const char* embedded_blob_data_symbol,
                                   const EmbeddedData* blob,
                                   const void* unwind_infos) {}

 protected:
  FILE* fp_ = nullptr;
};

// The factory function. Returns the appropriate platform-specific instance.
std::unique_ptr<PlatformEmbeddedFileWriterBase> NewPlatformEmbeddedFileWriter(
    const char* target_arch, const char* target_os);

#if V8_ENABLE_DRUMBRAKE
inline bool IsDrumBrakeInstructionHandler(const char* name) {
  std::string builtin_name(name);
  return builtin_name.find("Builtins_r2r_") == 0 ||
         builtin_name.find("Builtins_r2s_") == 0 ||
         builtin_name.find("Builtins_s2r_") == 0 ||
         builtin_name.find("Builtins_s2s_") == 0;
}
#endif  // V8_ENABLE_DRUMBRAKE

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_PLATFORM_EMBEDDED_FILE_WRITER_BASE_H_
