// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/embedded-file-writer.h"

#include <algorithm>
#include <cinttypes>

#include "src/codegen/source-position-table.h"
#include "src/flags/flags.h"  // For ENABLE_CONTROL_FLOW_INTEGRITY_BOOL
#include "src/objects/code-inl.h"
#include "src/snapshot/embedded/embedded-data-inl.h"

namespace v8 {
namespace internal {

namespace {

int WriteDirectiveOrSeparator(PlatformEmbeddedFileWriterBase* w,
                              int current_line_length,
                              DataDirective directive) {
  int printed_chars;
  if (current_line_length == 0) {
    printed_chars = w->IndentedDataDirective(directive);
    DCHECK_LT(0, printed_chars);
  } else {
    printed_chars = fprintf(w->fp(), ",");
    DCHECK_EQ(1, printed_chars);
  }
  return current_line_length + printed_chars;
}

int WriteLineEndIfNeeded(PlatformEmbeddedFileWriterBase* w,
                         int current_line_length, int write_size) {
  static const int kTextWidth = 100;
  // Check if adding ',0xFF...FF\n"' would force a line wrap. This doesn't use
  // the actual size of the string to be written to determine this so it's
  // more conservative than strictly needed.
  if (current_line_length + strlen(",0x") + write_size * 2 > kTextWidth) {
    fprintf(w->fp(), "\n");
    return 0;
  } else {
    return current_line_length;
  }
}

}  // namespace

void EmbeddedFileWriter::WriteBuiltin(PlatformEmbeddedFileWriterBase* w,
                                      const i::EmbeddedData* blob,
                                      const Builtin builtin) const {
  const bool is_default_variant =
      std::strcmp(embedded_variant_, kDefaultEmbeddedVariant) == 0;

  base::EmbeddedVector<char, kTemporaryStringLength> builtin_symbol;
  if (is_default_variant) {
    // Create nicer symbol names for the default mode.
    base::SNPrintF(builtin_symbol, "Builtins_%s", i::Builtins::name(builtin));
  } else {
    base::SNPrintF(builtin_symbol, "%s_Builtins_%s", embedded_variant_,
                   i::Builtins::name(builtin));
  }

  // Labels created here will show up in backtraces. We check in
  // Isolate::SetEmbeddedBlob that the blob layout remains unchanged, i.e.
  // that labels do not insert bytes into the middle of the blob byte
  // stream.
  w->DeclareFunctionBegin(builtin_symbol.begin(),
                          blob->InstructionSizeOf(builtin));
  const int builtin_id = static_cast<int>(builtin);
  const std::vector<uint8_t>& current_positions = source_positions_[builtin_id];
  // The code below interleaves bytes of assembly code for the builtin
  // function with source positions at the appropriate offsets.
  base::Vector<const uint8_t> vpos(current_positions.data(),
                                   current_positions.size());
  v8::internal::SourcePositionTableIterator positions(
      vpos, SourcePositionTableIterator::kExternalOnly);

#ifndef DEBUG
  CHECK(positions.done());  // Release builds must not contain debug infos.
#endif

  // Some builtins (InterpreterPushArgsThenFastConstructFunction,
  // JSConstructStubGeneric) have entry points located in the middle of them, we
  // need to store their addresses since they are part of the list of allowed
  // return addresses in the deoptimizer.
  const std::vector<LabelInfo>& current_labels = label_info_[builtin_id];
  auto label = current_labels.begin();

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(blob->InstructionStartOf(builtin));
  uint32_t size = blob->PaddedInstructionSizeOf(builtin);
  uint32_t i = 0;
  uint32_t next_source_pos_offset =
      static_cast<uint32_t>(positions.done() ? size : positions.code_offset());
  uint32_t next_label_offset = static_cast<uint32_t>(
      (label == current_labels.end()) ? size : label->offset);
  uint32_t next_offset = 0;
  while (i < size) {
    if (i == next_source_pos_offset) {
      // Write source directive.
      w->SourceInfo(positions.source_position().ExternalFileId(),
                    GetExternallyCompiledFilename(
                        positions.source_position().ExternalFileId()),
                    positions.source_position().ExternalLine());
      positions.Advance();
      next_source_pos_offset = static_cast<uint32_t>(
          positions.done() ? size : positions.code_offset());
      CHECK_GE(next_source_pos_offset, i);
    }
    if (i == next_label_offset) {
      WriteBuiltinLabels(w, label->name);
      label++;
      next_label_offset = static_cast<uint32_t>(
          (label == current_labels.end()) ? size : label->offset);
      CHECK_GE(next_label_offset, i);
    }
    next_offset = std::min(next_source_pos_offset, next_label_offset);
    WriteBinaryContentsAsInlineAssembly(w, data + i, next_offset - i);
    i = next_offset;
  }

  w->DeclareFunctionEnd(builtin_symbol.begin());
}

void EmbeddedFileWriter::WriteBuiltinLabels(PlatformEmbeddedFileWriterBase* w,
                                            std::string name) const {
  w->DeclareLabel(name.c_str());
}

void EmbeddedFileWriter::WriteCodeSection(PlatformEmbeddedFileWriterBase* w,
                                          const i::EmbeddedData* blob) const {
  w->Comment(
      "The embedded blob code section starts here. It contains the builtin");
  w->Comment("instruction streams.");
  w->SectionText();

#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
  // UMA needs an exposed function-type label at the start of the embedded
  // code section.
  static const char* kCodeStartForProfilerSymbolName =
      "v8_code_start_for_profiler_";
  static constexpr int kDummyFunctionLength = 1;
  static constexpr int kDummyFunctionData = 0xcc;
  w->DeclareFunctionBegin(kCodeStartForProfilerSymbolName,
                          kDummyFunctionLength);
  // The label must not be at the same address as the first builtin, insert
  // padding bytes.
  WriteDirectiveOrSeparator(w, 0, kByte);
  w->HexLiteral(kDummyFunctionData);
  w->Newline();
  w->DeclareFunctionEnd(kCodeStartForProfilerSymbolName);
#endif

  w->AlignToCodeAlignment();
  w->DeclareSymbolGlobal(EmbeddedBlobCodeSymbol().c_str());
  w->DeclareLabelProlog(EmbeddedBlobCodeSymbol().c_str());
  w->DeclareLabel(EmbeddedBlobCodeSymbol().c_str());

  static_assert(Builtins::kAllBuiltinsAreIsolateIndependent);
  for (ReorderedBuiltinIndex embedded_index = 0;
       embedded_index < Builtins::kBuiltinCount; embedded_index++) {
    Builtin builtin = blob->GetBuiltinId(embedded_index);
    WriteBuiltin(w, blob, builtin);
  }
  w->AlignToPageSizeIfNeeded();
  w->DeclareLabelEpilogue();
  w->Newline();
}

void EmbeddedFileWriter::WriteFileEpilogue(PlatformEmbeddedFileWriterBase* w,
                                           const i::EmbeddedData* blob) const {
  {
    base::EmbeddedVector<char, kTemporaryStringLength>
        embedded_blob_code_size_symbol;
    base::SNPrintF(embedded_blob_code_size_symbol,
                   "v8_%s_embedded_blob_code_size_", embedded_variant_);

    w->Comment("The size of the embedded blob code in bytes.");
    w->SectionRoData();
    w->AlignToDataAlignment();
    w->DeclareUint32(embedded_blob_code_size_symbol.begin(), blob->code_size());
    w->Newline();

    base::EmbeddedVector<char, kTemporaryStringLength>
        embedded_blob_data_size_symbol;
    base::SNPrintF(embedded_blob_data_size_symbol,
                   "v8_%s_embedded_blob_data_size_", embedded_variant_);

    w->Comment("The size of the embedded blob data section in bytes.");
    w->DeclareUint32(embedded_blob_data_size_symbol.begin(), blob->data_size());
    w->Newline();
  }

#if defined(V8_OS_WIN64)
  {
    base::EmbeddedVector<char, kTemporaryStringLength> unwind_info_symbol;
    base::SNPrintF(unwind_info_symbol, "%s_Builtins_UnwindInfo",
                   embedded_variant_);

    w->MaybeEmitUnwindData(unwind_info_symbol.begin(),
                           EmbeddedBlobCodeSymbol().c_str(), blob,
                           reinterpret_cast<const void*>(&unwind_infos_[0]));
  }
#endif  // V8_OS_WIN64

  w->FileEpilogue();
}

// static
void EmbeddedFileWriter::WriteBinaryContentsAsInlineAssembly(
    PlatformEmbeddedFileWriterBase* w, const uint8_t* data, uint32_t size) {
#if V8_OS_ZOS
  // HLASM source must end at column 71 (followed by an optional
  // line-continuation char on column 72), so write the binary data
  // in 32 byte chunks (length 64):
  uint32_t chunks = (size + 31) / 32;
  uint32_t i, j;
  uint32_t offset = 0;
  for (i = 0; i < chunks; ++i) {
    fprintf(w->fp(), " DC x'");
    for (j = 0; offset < size && j < 32; ++j) {
      fprintf(w->fp(), "%02x", data[offset++]);
    }
    fprintf(w->fp(), "'\n");
  }
#else
  int current_line_length = 0;
  uint32_t i = 0;

  // Begin by writing out byte chunks.
  const DataDirective directive = w->ByteChunkDataDirective();
  const int byte_chunk_size = DataDirectiveSize(directive);
  for (; i + byte_chunk_size < size; i += byte_chunk_size) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, directive);
    current_line_length += w->WriteByteChunk(data + i);
    current_line_length =
        WriteLineEndIfNeeded(w, current_line_length, byte_chunk_size);
  }
  if (current_line_length != 0) w->Newline();
  current_line_length = 0;

  // Write any trailing bytes one-by-one.
  for (; i < size; i++) {
    current_line_length =
        WriteDirectiveOrSeparator(w, current_line_length, kByte);
    current_line_length += w->HexLiteral(data[i]);
    current_line_length = WriteLineEndIfNeeded(w, current_line_length, 1);
  }

  if (current_line_length != 0) w->Newline();
#endif  // V8_OS_ZOS
}

int EmbeddedFileWriter::LookupOrAddExternallyCompiledFilename(
    const char* filename) {
  auto result = external_filenames_.find(filename);
  if (result != external_filenames_.end()) {
    return result->second;
  }
  int new_id =
      ExternalFilenameIndexToId(static_cast<int>(external_filenames_.size()));
  external_filenames_.insert(std::make_pair(filename, new_id));
  external_filenames_by_index_.push_back(filename);
  DCHECK_EQ(external_filenames_by_index_.size(), external_filenames_.size());
  return new_id;
}

const char* EmbeddedFileWriter::GetExternallyCompiledFilename(
    int fileid) const {
  size_t index = static_cast<size_t>(ExternalFilenameIdToIndex(fileid));
  DCHECK_GE(index, 0);
  DCHECK_LT(index, external_filenames_by_index_.size());

  return external_filenames_by_index_[index];
}

int EmbeddedFileWriter::GetExternallyCompiledFilenameCount() const {
  return static_cast<int>(external_filenames_.size());
}

void EmbeddedFileWriter::PrepareBuiltinSourcePositionMap(Builtins* builtins) {
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    // Retrieve the SourcePositionTable and copy it.
    Tagged<Code> code = builtins->code(builtin);
    if (!code->has_source_position_table()) continue;
    Tagged<TrustedByteArray> source_position_table =
        code->source_position_table();
    std::vector<unsigned char> data(source_position_table->begin(),
                                    source_position_table->end());
    source_positions_[static_cast<int>(builtin)] = data;
  }
}

}  // namespace internal
}  // namespace v8
