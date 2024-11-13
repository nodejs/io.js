// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"

#include <algorithm>

#include "src/common/globals.h"  // For V8_OS_WIN64

#if defined(V8_OS_WIN64)
#include "src/builtins/builtins.h"
#include "src/diagnostics/unwinding-info-win64.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/embedded/embedded-file-writer.h"
#endif  // V8_OS_WIN64

// V8_CC_MSVC is true for both MSVC and clang on windows. clang can handle
// __asm__-style inline assembly but MSVC cannot, and thus we need a more
// precise compiler detection that can distinguish between the two. clang on
// windows sets both __clang__ and _MSC_VER, MSVC sets only _MSC_VER.
#if defined(_MSC_VER) && !defined(__clang__)
#define V8_COMPILER_IS_MSVC
#endif

#if defined(V8_COMPILER_IS_MSVC)
#include "src/flags/flags.h"
#endif

namespace v8 {
namespace internal {

// MSVC uses MASM for x86 and x64, while it has a ARMASM for ARM32 and
// ARMASM64 for ARM64. Since ARMASM and ARMASM64 accept a slightly tweaked
// version of ARM assembly language, they are referred to together in Visual
// Studio project files as MARMASM.
//
// ARM assembly language docs:
// http://infocenter.arm.com/help/topic/com.arm.doc.dui0802b/index.html
// Microsoft ARM assembler and assembly language docs:
// https://docs.microsoft.com/en-us/cpp/assembler/arm/arm-assembler-reference

// Name mangling.
// Symbols are prefixed with an underscore on 32-bit architectures.
#if !defined(V8_TARGET_ARCH_X64) && !defined(V8_TARGET_ARCH_ARM64)
#define SYMBOL_PREFIX "_"
#else
#define SYMBOL_PREFIX ""
#endif

// Notes:
//
// Cross-bitness builds are unsupported. It's thus safe to detect bitness
// through compile-time defines.
//
// Cross-compiler builds (e.g. with mixed use of clang / MSVC) are likewise
// unsupported and hence the compiler can also be detected through compile-time
// defines.

namespace {

#if defined(V8_OS_WIN_X64)

void WriteUnwindInfoEntry(PlatformEmbeddedFileWriterWin* w,
                          const char* unwind_info_symbol,
                          const char* embedded_blob_data_symbol,
                          uint64_t rva_start, uint64_t rva_end) {
  w->DeclareRvaToSymbol(embedded_blob_data_symbol, rva_start);
  w->DeclareRvaToSymbol(embedded_blob_data_symbol, rva_end);
  w->DeclareRvaToSymbol(unwind_info_symbol);
}

void EmitUnwindData(PlatformEmbeddedFileWriterWin* w,
                    const char* unwind_info_symbol,
                    const char* embedded_blob_data_symbol,
                    const EmbeddedData* blob,
                    const win64_unwindinfo::BuiltinUnwindInfo* unwind_infos) {
  // Emit an UNWIND_INFO (XDATA) struct, which contains the unwinding
  // information that is used for all builtin functions.
  DCHECK(win64_unwindinfo::CanEmitUnwindInfoForBuiltins());
  w->Comment("xdata for all the code in the embedded blob.");
  w->DeclareExternalFunction(CRASH_HANDLER_FUNCTION_NAME_STRING);

  w->StartXdataSection();
  {
    w->DeclareLabel(unwind_info_symbol);

    std::vector<uint8_t> xdata =
        win64_unwindinfo::GetUnwindInfoForBuiltinFunctions();
    DCHECK(!xdata.empty());

    w->IndentedDataDirective(kByte);
    for (size_t i = 0; i < xdata.size(); i++) {
      if (i > 0) fprintf(w->fp(), ",");
      w->HexLiteral(xdata[i]);
    }
    w->Newline();

    w->Comment("    ExceptionHandler");
    w->DeclareRvaToSymbol(CRASH_HANDLER_FUNCTION_NAME_STRING);
  }
  w->EndXdataSection();
  w->Newline();

  // Emit a RUNTIME_FUNCTION (PDATA) entry for each builtin function, as
  // documented here:
  // https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64.
  w->Comment(
      "pdata for all the code in the embedded blob (structs of type "
      "RUNTIME_FUNCTION).");
  w->Comment("    BeginAddress");
  w->Comment("    EndAddress");
  w->Comment("    UnwindInfoAddress");
  w->StartPdataSection();
  {
    static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
    Address prev_builtin_end_offset = 0;
    for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
         ++builtin) {
      const int builtin_index = static_cast<int>(builtin);
      // Some builtins are leaf functions from the point of view of Win64 stack
      // walking: they do not move the stack pointer and do not require a PDATA
      // entry because the return address can be retrieved from [rsp].
      if (unwind_infos[builtin_index].is_leaf_function()) continue;

      uint64_t builtin_start_offset = blob->InstructionStartOf(builtin) -
                                      reinterpret_cast<Address>(blob->code());
      uint32_t builtin_size = blob->InstructionSizeOf(builtin);

      const std::vector<int>& xdata_desc =
          unwind_infos[builtin_index].fp_offsets();
      if (xdata_desc.empty()) {
        // Some builtins do not have any "push rbp - mov rbp, rsp" instructions
        // to start a stack frame. We still emit a PDATA entry as if they had,
        // relying on the fact that we can find the previous frame address from
        // rbp in most cases. Note that since the function does not really start
        // with a 'push rbp' we need to specify the start RVA in the PDATA entry
        // a few bytes before the beginning of the function, if it does not
        // overlap the end of the previous builtin.
        WriteUnwindInfoEntry(
            w, unwind_info_symbol, embedded_blob_data_symbol,
            std::max(prev_builtin_end_offset,
                     builtin_start_offset - win64_unwindinfo::kRbpPrefixLength),
            builtin_start_offset + builtin_size);
      } else {
        // Some builtins have one or more "push rbp - mov rbp, rsp" sequences,
        // but not necessarily at the beginning of the function. In this case
        // we want to yield a PDATA entry for each block of instructions that
        // emit an rbp frame. If the function does not start with 'push rbp'
        // we also emit a PDATA entry for the initial block of code up to the
        // first 'push rbp', like in the case above.
        if (xdata_desc[0] > 0) {
          WriteUnwindInfoEntry(w, unwind_info_symbol, embedded_blob_data_symbol,
                               std::max(prev_builtin_end_offset,
                                        builtin_start_offset -
                                            win64_unwindinfo::kRbpPrefixLength),
                               builtin_start_offset + xdata_desc[0]);
        }

        for (size_t j = 0; j < xdata_desc.size(); j++) {
          int chunk_start = xdata_desc[j];
          int chunk_end =
              (j < xdata_desc.size() - 1) ? xdata_desc[j + 1] : builtin_size;
          WriteUnwindInfoEntry(w, unwind_info_symbol, embedded_blob_data_symbol,
                               builtin_start_offset + chunk_start,
                               builtin_start_offset + chunk_end);
        }
      }

      prev_builtin_end_offset = builtin_start_offset + builtin_size;
      w->Newline();
    }
  }
  w->EndPdataSection();
  w->Newline();
}

#elif defined(V8_OS_WIN_ARM64)

void EmitUnwindData(PlatformEmbeddedFileWriterWin* w,
                    const char* unwind_info_symbol,
                    const char* embedded_blob_data_symbol,
                    const EmbeddedData* blob,
                    const win64_unwindinfo::BuiltinUnwindInfo* unwind_infos) {
  DCHECK(win64_unwindinfo::CanEmitUnwindInfoForBuiltins());

  // Fairly arbitrary but should fit all symbol names.
  static constexpr int kTemporaryStringLength = 256;
  base::EmbeddedVector<char, kTemporaryStringLength> unwind_info_full_symbol;

  // Emit a RUNTIME_FUNCTION (PDATA) entry for each builtin function, as
  // documented here:
  // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling.
  w->Comment(
      "pdata for all the code in the embedded blob (structs of type "
      "RUNTIME_FUNCTION).");
  w->Comment("    BeginAddress");
  w->Comment("    UnwindInfoAddress");
  w->StartPdataSection();
  std::vector<int> code_chunks;
  std::vector<win64_unwindinfo::FrameOffsets> fp_adjustments;

  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    const int builtin_index = static_cast<int>(builtin);
    if (unwind_infos[builtin_index].is_leaf_function()) continue;

    uint64_t builtin_start_offset = blob->InstructionStartOf(builtin) -
                                    reinterpret_cast<Address>(blob->code());
    uint32_t builtin_size = blob->InstructionSizeOf(builtin);

    const std::vector<int>& xdata_desc =
        unwind_infos[builtin_index].fp_offsets();
    const std::vector<win64_unwindinfo::FrameOffsets>& xdata_fp_adjustments =
        unwind_infos[builtin_index].fp_adjustments();
    DCHECK_EQ(xdata_desc.size(), xdata_fp_adjustments.size());

    for (size_t j = 0; j < xdata_desc.size(); j++) {
      int chunk_start = xdata_desc[j];
      int chunk_end =
          (j < xdata_desc.size() - 1) ? xdata_desc[j + 1] : builtin_size;
      int chunk_len = ::RoundUp(chunk_end - chunk_start, kInstrSize);

      while (chunk_len > 0) {
        int allowed_chunk_len =
            std::min(chunk_len, win64_unwindinfo::kMaxFunctionLength);
        chunk_len -= win64_unwindinfo::kMaxFunctionLength;

        // Record the chunk length and fp_adjustment for emitting UNWIND_INFO
        // later.
        code_chunks.push_back(allowed_chunk_len);
        fp_adjustments.push_back(xdata_fp_adjustments[j]);
        base::SNPrintF(unwind_info_full_symbol, "%s_%u", unwind_info_symbol,
                       code_chunks.size());
        w->DeclareRvaToSymbol(embedded_blob_data_symbol,
                              builtin_start_offset + chunk_start);
        w->DeclareRvaToSymbol(unwind_info_full_symbol.begin());
      }
    }
  }
  w->EndPdataSection();
  w->Newline();

  // Emit an UNWIND_INFO (XDATA) structs, which contains the unwinding
  // information.
  w->DeclareExternalFunction(CRASH_HANDLER_FUNCTION_NAME_STRING);
  w->StartXdataSection();
  {
    for (size_t i = 0; i < code_chunks.size(); i++) {
      base::SNPrintF(unwind_info_full_symbol, "%s_%u", unwind_info_symbol,
                     i + 1);
      w->DeclareLabel(unwind_info_full_symbol.begin());
      std::vector<uint8_t> xdata =
          win64_unwindinfo::GetUnwindInfoForBuiltinFunction(code_chunks[i],
                                                            fp_adjustments[i]);

      w->IndentedDataDirective(kByte);
      for (size_t j = 0; j < xdata.size(); j++) {
        if (j > 0) fprintf(w->fp(), ",");
        w->HexLiteral(xdata[j]);
      }
      w->Newline();
      w->DeclareRvaToSymbol(CRASH_HANDLER_FUNCTION_NAME_STRING);
    }
  }
  w->EndXdataSection();
  w->Newline();
}

#endif  // V8_OS_WIN_X64

}  // namespace

const char* PlatformEmbeddedFileWriterWin::DirectiveAsString(
    DataDirective directive) {
#if defined(V8_COMPILER_IS_MSVC)
  if (target_arch_ != EmbeddedTargetArch::kArm64) {
    switch (directive) {
      case kByte:
        return "BYTE";
      case kLong:
        return "DWORD";
      case kQuad:
        return "QWORD";
      default:
        UNREACHABLE();
    }
  } else {
    switch (directive) {
      case kByte:
        return "DCB";
      case kLong:
        return "DCDU";
      case kQuad:
        return "DCQU";
      default:
        UNREACHABLE();
    }
  }
#else
  switch (directive) {
    case kByte:
      return ".byte";
    case kLong:
      return ".long";
    case kQuad:
      return ".quad";
    case kOcta:
      return ".octa";
  }
  UNREACHABLE();
#endif
}

void PlatformEmbeddedFileWriterWin::MaybeEmitUnwindData(
    const char* unwind_info_symbol, const char* embedded_blob_data_symbol,
    const EmbeddedData* blob, const void* unwind_infos) {
// Windows ARM64 supports cross build which could require unwind info for
// host_os. Ignore this case because it is only used in build time.
#if defined(V8_OS_WIN_ARM64)
  if (target_arch_ != EmbeddedTargetArch::kArm64) {
    return;
  }
#endif  // V8_OS_WIN_ARM64

#if defined(V8_OS_WIN64)
  if (win64_unwindinfo::CanEmitUnwindInfoForBuiltins()) {
    EmitUnwindData(this, unwind_info_symbol, embedded_blob_data_symbol, blob,
                   reinterpret_cast<const win64_unwindinfo::BuiltinUnwindInfo*>(
                       unwind_infos));
  }
#endif  // V8_OS_WIN64
}

// Windows, MSVC
// -----------------------------------------------------------------------------

#if defined(V8_COMPILER_IS_MSVC)

// For x64 MSVC builds we emit assembly in MASM syntax.
// See https://docs.microsoft.com/en-us/cpp/assembler/masm/directives-reference.
// For Arm build, we emit assembly in MARMASM syntax.
// Note that the same mksnapshot has to be used to compile the host and target.

// The AARCH64 ABI requires instructions be 4-byte-aligned and Windows does
// not have a stricter alignment requirement (see the TEXTAREA macro of
// kxarm64.h in the Windows SDK), so code is 4-byte-aligned.
// The data fields in the emitted assembly tend to be accessed with 8-byte
// LDR instructions, so data is 8-byte-aligned.
//
// armasm64's warning A4228 states
//     Alignment value exceeds AREA alignment; alignment not guaranteed
// To ensure that ALIGN directives are honored, their values are defined as
// equal to their corresponding AREA's ALIGN attributes.

#define ARM64_DATA_ALIGNMENT_POWER (3)
#define ARM64_DATA_ALIGNMENT (1 << ARM64_DATA_ALIGNMENT_POWER)
#define ARM64_CODE_ALIGNMENT_POWER (2)
#define ARM64_CODE_ALIGNMENT (1 << ARM64_CODE_ALIGNMENT_POWER)

void PlatformEmbeddedFileWriterWin::SectionText() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  AREA |.text|, CODE, ALIGN=%d, READONLY\n",
            ARM64_CODE_ALIGNMENT_POWER);
  } else {
    fprintf(fp_, ".CODE\n");
  }
}

void PlatformEmbeddedFileWriterWin::SectionRoData() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  AREA |.rodata|, DATA, ALIGN=%d, READONLY\n",
            ARM64_DATA_ALIGNMENT_POWER);
  } else {
    fprintf(fp_, ".CONST\n");
  }
}

void PlatformEmbeddedFileWriterWin::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  fprintf(fp_, "%s%s %s %d\n", SYMBOL_PREFIX, name, DirectiveAsString(kLong),
          value);
}

void PlatformEmbeddedFileWriterWin::StartPdataSection() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  AREA |.pdata|, DATA, ALIGN=%d, READONLY\n",
            ARM64_DATA_ALIGNMENT_POWER);
  } else {
    fprintf(fp_, "OPTION DOTNAME\n");
    fprintf(fp_, ".pdata SEGMENT DWORD READ ''\n");
  }
}

void PlatformEmbeddedFileWriterWin::EndPdataSection() {
  if (target_arch_ != EmbeddedTargetArch::kArm64) {
    fprintf(fp_, ".pdata ENDS\n");
  }
}

void PlatformEmbeddedFileWriterWin::StartXdataSection() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  AREA |.xdata|, DATA, ALIGN=%d, READONLY\n",
            ARM64_DATA_ALIGNMENT_POWER);
  } else {
    fprintf(fp_, "OPTION DOTNAME\n");
    fprintf(fp_, ".xdata SEGMENT DWORD READ ''\n");
  }
}

void PlatformEmbeddedFileWriterWin::EndXdataSection() {
  if (target_arch_ != EmbeddedTargetArch::kArm64) {
    fprintf(fp_, ".xdata ENDS\n");
  }
}

void PlatformEmbeddedFileWriterWin::DeclareExternalFunction(const char* name) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  EXTERN %s \n", name);
  } else {
    fprintf(fp_, "EXTERN %s : PROC\n", name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareRvaToSymbol(const char* name,
                                                       uint64_t offset) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    if (offset > 0) {
      fprintf(fp_, "  DCD  %s + %llu\n", name, offset);
    } else {
      fprintf(fp_, "  DCD  %s\n", name);
    }
    // The default relocation entry generated by MSVC armasm64.exe for DCD
    // directive is IMAGE_REL_ARM64_ADDR64 which represents relocation for
    // 64-bit pointer instead of 32-bit RVA. Append RELOC with
    // IMAGE_REL_ARM64_ADDR32NB(2) to generate correct relocation entry for
    // 32-bit RVA.
    fprintf(fp_, "  RELOC 2\n");
  } else {
    if (offset > 0) {
      fprintf(fp_, "DD IMAGEREL %s+%llu\n", name, offset);
    } else {
      fprintf(fp_, "DD IMAGEREL %s\n", name);
    }
  }
}

void PlatformEmbeddedFileWriterWin::DeclareSymbolGlobal(const char* name) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  EXPORT %s%s\n", SYMBOL_PREFIX, name);
  } else {
    fprintf(fp_, "PUBLIC %s%s\n", SYMBOL_PREFIX, name);
  }
}

void PlatformEmbeddedFileWriterWin::AlignToCodeAlignment() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  ALIGN %d\n", ARM64_CODE_ALIGNMENT);
  } else {
    // Diverges from other platforms due to compile error
    // 'invalid combination with segment alignment'.
    fprintf(fp_, "ALIGN 4\n");
  }
}

void PlatformEmbeddedFileWriterWin::AlignToDataAlignment() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  ALIGN %d\n", ARM64_DATA_ALIGNMENT);

  } else {
    fprintf(fp_, "ALIGN 4\n");
  }
}

void PlatformEmbeddedFileWriterWin::Comment(const char* string) {
  fprintf(fp_, "; %s\n", string);
}

void PlatformEmbeddedFileWriterWin::DeclareLabel(const char* name) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "%s%s\n", SYMBOL_PREFIX, name);

  } else {
    fprintf(fp_, "%s%s LABEL %s\n", SYMBOL_PREFIX, name,
            DirectiveAsString(kByte));
  }
}

void PlatformEmbeddedFileWriterWin::SourceInfo(int fileid, const char* filename,
                                               int line) {
  // TODO(mvstanton): output source information for MSVC.
  // Its syntax is #line <line> "<filename>"
}

// TODO(mmarchini): investigate emitting size annotations for Windows
void PlatformEmbeddedFileWriterWin::DeclareFunctionBegin(const char* name,
                                                         uint32_t size) {
  if (ENABLE_CONTROL_FLOW_INTEGRITY_BOOL) {
    DeclareSymbolGlobal(name);
  }

  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "%s%s FUNCTION\n", SYMBOL_PREFIX, name);

  } else {
    fprintf(fp_, "%s%s PROC\n", SYMBOL_PREFIX, name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionEnd(const char* name) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  ENDFUNC\n");

  } else {
    fprintf(fp_, "%s%s ENDP\n", SYMBOL_PREFIX, name);
  }
}

int PlatformEmbeddedFileWriterWin::HexLiteral(uint64_t value) {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    return fprintf(fp_, "0x%" PRIx64, value);

  } else {
    return fprintf(fp_, "0%" PRIx64 "h", value);
  }
}

void PlatformEmbeddedFileWriterWin::FilePrologue() {
  if (target_arch_ != EmbeddedTargetArch::kArm64 &&
      target_arch_ != EmbeddedTargetArch::kX64) {
    // x86 falls into this case
    fprintf(fp_, ".MODEL FLAT\n");
  }
}

void PlatformEmbeddedFileWriterWin::DeclareExternalFilename(
    int fileid, const char* filename) {}

void PlatformEmbeddedFileWriterWin::FileEpilogue() {
  if (target_arch_ == EmbeddedTargetArch::kArm64) {
    fprintf(fp_, "  END\n");
  } else {
    fprintf(fp_, "END\n");
  }
}

int PlatformEmbeddedFileWriterWin::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

#undef ARM64_DATA_ALIGNMENT_POWER
#undef ARM64_DATA_ALIGNMENT
#undef ARM64_CODE_ALIGNMENT_POWER
#undef ARM64_CODE_ALIGNMENT

// All Windows builds without MSVC.
// -----------------------------------------------------------------------------

#else

// The directives for text section prefix come from the COFF
// (Common Object File Format) standards:
// https://llvm.org/docs/Extensions.html
//
// .text$hot means this section contains hot code.
// x means executable section.
// r means read-only section.
void PlatformEmbeddedFileWriterWin::SectionText() {
  fprintf(fp_, ".section .text$hot,\"xr\"\n");
}

void PlatformEmbeddedFileWriterWin::SectionRoData() {
  fprintf(fp_, ".section .rdata\n");
}

void PlatformEmbeddedFileWriterWin::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  DeclareLabel(name);
  IndentedDataDirective(kLong);
  fprintf(fp_, "%d", value);
  Newline();
}

void PlatformEmbeddedFileWriterWin::StartPdataSection() {
  fprintf(fp_, ".section .pdata\n");
}

void PlatformEmbeddedFileWriterWin::EndPdataSection() {}

void PlatformEmbeddedFileWriterWin::StartXdataSection() {
  fprintf(fp_, ".section .xdata\n");
}

void PlatformEmbeddedFileWriterWin::EndXdataSection() {}

void PlatformEmbeddedFileWriterWin::DeclareExternalFunction(const char* name) {}

void PlatformEmbeddedFileWriterWin::DeclareRvaToSymbol(const char* name,
                                                       uint64_t offset) {
  if (offset > 0) {
    fprintf(fp_, ".rva %s + %" PRIu64 "\n", name, offset);
  } else {
    fprintf(fp_, ".rva %s\n", name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareSymbolGlobal(const char* name) {
  fprintf(fp_, ".global %s%s\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::AlignToCodeAlignment() {
#if V8_TARGET_ARCH_X64
  // On x64 use 64-bytes code alignment to allow 64-bytes loop header alignment.
  static_assert(64 >= kCodeAlignment);
  fprintf(fp_, ".balign 64\n");
#elif V8_TARGET_ARCH_PPC64
  // 64 byte alignment is needed on ppc64 to make sure p10 prefixed instructions
  // don't cross 64-byte boundaries.
  static_assert(64 >= kCodeAlignment);
  fprintf(fp_, ".balign 64\n");
#else
  static_assert(32 >= kCodeAlignment);
  fprintf(fp_, ".balign 32\n");
#endif
}

void PlatformEmbeddedFileWriterWin::AlignToDataAlignment() {
  // On Windows ARM64, s390, PPC and possibly more platforms, aligned load
  // instructions are used to retrieve v8_Default_embedded_blob_ and/or
  // v8_Default_embedded_blob_size_. The generated instructions require the
  // load target to be aligned at 8 bytes (2^3).
  fprintf(fp_, ".balign 8\n");
}

void PlatformEmbeddedFileWriterWin::Comment(const char* string) {
  fprintf(fp_, "// %s\n", string);
}

void PlatformEmbeddedFileWriterWin::DeclareLabel(const char* name) {
  fprintf(fp_, "%s%s:\n", SYMBOL_PREFIX, name);
}

void PlatformEmbeddedFileWriterWin::SourceInfo(int fileid, const char* filename,
                                               int line) {
  // BUG(9944): Use .cv_loc to ensure CodeView information is used on
  // Windows.
}

// TODO(mmarchini): investigate emitting size annotations for Windows
void PlatformEmbeddedFileWriterWin::DeclareFunctionBegin(const char* name,
                                                         uint32_t size) {
  DeclareLabel(name);

  if (target_arch_ == EmbeddedTargetArch::kArm64
#if V8_ENABLE_DRUMBRAKE
      || IsDrumBrakeInstructionHandler(name)
#endif  // V8_ENABLE_DRUMBRAKE
  ) {
    // Windows ARM64 assembly is in GAS syntax, but ".type" is invalid directive
    // in PE/COFF for Windows.
    DeclareSymbolGlobal(name);
  } else {
    // The directives for inserting debugging information on Windows come
    // from the PE (Portable Executable) and COFF (Common Object File Format)
    // standards. Documented here:
    // https://docs.microsoft.com/en-us/windows/desktop/debug/pe-format
    //
    // .scl 2 means StorageClass external.
    // .type 32 means Type Representation Function.
    fprintf(fp_, ".def %s%s; .scl 2; .type 32; .endef;\n", SYMBOL_PREFIX, name);
  }
}

void PlatformEmbeddedFileWriterWin::DeclareFunctionEnd(const char* name) {}

int PlatformEmbeddedFileWriterWin::HexLiteral(uint64_t value) {
  return fprintf(fp_, "0x%" PRIx64, value);
}

void PlatformEmbeddedFileWriterWin::FilePrologue() {}

void PlatformEmbeddedFileWriterWin::DeclareExternalFilename(
    int fileid, const char* filename) {
  // BUG(9944): Use .cv_filename to ensure CodeView information is used on
  // Windows.
}

void PlatformEmbeddedFileWriterWin::FileEpilogue() {}

int PlatformEmbeddedFileWriterWin::IndentedDataDirective(
    DataDirective directive) {
  return fprintf(fp_, "  %s ", DirectiveAsString(directive));
}

#endif

DataDirective PlatformEmbeddedFileWriterWin::ByteChunkDataDirective() const {
#if defined(V8_COMPILER_IS_MSVC)
  // Windows MASM doesn't have an .octa directive, use QWORDs instead.
  // Note: MASM *really* does not like large data streams. It takes over 5
  // minutes to assemble the ~350K lines of embedded.S produced when using
  // BYTE directives in a debug build. QWORD produces roughly 120KLOC and
  // reduces assembly time to ~40 seconds. Still terrible, but much better
  // than before. See also: https://crbug.com/v8/8475.
  return kQuad;
#else
  return PlatformEmbeddedFileWriterBase::ByteChunkDataDirective();
#endif
}

int PlatformEmbeddedFileWriterWin::WriteByteChunk(const uint8_t* data) {
#if defined(V8_COMPILER_IS_MSVC)
  DCHECK_EQ(ByteChunkDataDirective(), kQuad);
  const uint64_t* quad_ptr = reinterpret_cast<const uint64_t*>(data);
  return HexLiteral(*quad_ptr);
#else
  return PlatformEmbeddedFileWriterBase::WriteByteChunk(data);
#endif
}

#undef SYMBOL_PREFIX
#undef V8_ASSEMBLER_IS_MASM
#undef V8_ASSEMBLER_IS_MARMASM
#undef V8_COMPILER_IS_MSVC

}  // namespace internal
}  // namespace v8
