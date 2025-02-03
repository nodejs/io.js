// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-zos.h"

#include <stdarg.h>

#include <string>

namespace v8 {
namespace internal {

// https://www.ibm.com/docs/en/zos/2.1.0?topic=conventions-continuation-lines
// for length of HLASM statements and continuation.
static constexpr int kAsmMaxLineLen = 71;
static constexpr int kAsmContIndentLen = 15;
static constexpr int kAsmContMaxLen = kAsmMaxLineLen - kAsmContIndentLen;

namespace {
int hlasmPrintLine(FILE* fp, const char* fmt, ...) {
  int ret;
  char buffer[4096];
  int offset = 0;
  static char indent[kAsmContIndentLen] = "";
  va_list ap;
  va_start(ap, fmt);
  ret = vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);
  if (!*indent) memset(indent, ' ', sizeof(indent));
  if (ret > kAsmMaxLineLen && buffer[kAsmMaxLineLen] != '\n') {
    offset += fwrite(buffer + offset, 1, kAsmMaxLineLen, fp);
    // Write continuation mark
    fwrite("-\n", 1, 2, fp);
    ret -= kAsmMaxLineLen;
    while (ret > kAsmContMaxLen) {
      // indent by kAsmContIndentLen
      fwrite(indent, 1, kAsmContIndentLen, fp);
      offset += fwrite(buffer + offset, 1, kAsmContMaxLen, fp);
      // write continuation mark
      fwrite("-\n", 1, 2, fp);
      ret -= kAsmContMaxLen;
    }
    if (ret > 0) {
      // indent kAsmContIndentLen blanks
      fwrite(indent, 1, kAsmContIndentLen, fp);
      offset += fwrite(buffer + offset, 1, ret, fp);
    }
  } else {
    offset += fwrite(buffer + offset, 1, ret, fp);
  }
  return ret;
}
}  // namespace

void PlatformEmbeddedFileWriterZOS::DeclareLabelProlog(const char* name) {
  fprintf(fp_,
          "&suffix SETA &suffix+1\n"
          "CEECWSA LOCTR\n"
          "AL&suffix ALIAS C'%s'\n"
          "C_WSA64 CATTR DEFLOAD,RMODE(64),PART(AL&suffix)\n"
          "AL&suffix XATTR REF(DATA),LINKAGE(XPLINK),SCOPE(EXPORT)\n",
          name);
}

void PlatformEmbeddedFileWriterZOS::DeclareLabelEpilogue() {
  fprintf(fp_,
          "C_WSA64 CATTR PART(PART1)\n"
          "LBL&suffix DC AD(AL&suffix)\n");
}

void PlatformEmbeddedFileWriterZOS::DeclareUint32(const char* name,
                                                  uint32_t value) {
  DeclareSymbolGlobal(name);
  fprintf(fp_,
          "&suffix SETA &suffix+1\n"
          "CEECWSA LOCTR\n"
          "AL&suffix ALIAS C'%s'\n"
          "C_WSA64 CATTR DEFLOAD,RMODE(64),PART(AL&suffix)\n"
          "AL&suffix XATTR REF(DATA),LINKAGE(XPLINK),SCOPE(EXPORT)\n"
          " DC F'%d'\n"
          "C_WSA64 CATTR PART(PART1)\n"
          "LBL&suffix DC AD(AL&suffix)\n",
          name, value);
}

void PlatformEmbeddedFileWriterZOS::DeclareSymbolGlobal(const char* name) {
  hlasmPrintLine(fp_, "* Global Symbol %s\n", name);
}

void PlatformEmbeddedFileWriterZOS::AlignToCodeAlignment() {
  // No code alignment required.
}

void PlatformEmbeddedFileWriterZOS::AlignToDataAlignment() {
  // No data alignment required.
}

void PlatformEmbeddedFileWriterZOS::Comment(const char* string) {
  hlasmPrintLine(fp_, "* %s\n", string);
}

void PlatformEmbeddedFileWriterZOS::DeclareLabel(const char* name) {
  hlasmPrintLine(fp_, "*--------------------------------------------\n");
  hlasmPrintLine(fp_, "* Label %s\n", name);
  hlasmPrintLine(fp_, "*--------------------------------------------\n");
  hlasmPrintLine(fp_, "%s DS 0H\n", name);
}

void PlatformEmbeddedFileWriterZOS::SourceInfo(int fileid, const char* filename,
                                               int line) {
  hlasmPrintLine(fp_, "* line %d \"%s\"\n", line, filename);
}

void PlatformEmbeddedFileWriterZOS::DeclareFunctionBegin(const char* name,
                                                         uint32_t size) {
  hlasmPrintLine(fp_, "*--------------------------------------------\n");
  hlasmPrintLine(fp_, "* Builtin %s\n", name);
  hlasmPrintLine(fp_, "*--------------------------------------------\n");
  hlasmPrintLine(fp_, "%s DS 0H\n", name);
}

void PlatformEmbeddedFileWriterZOS::DeclareFunctionEnd(const char* name) {
  // Not used.
}

int PlatformEmbeddedFileWriterZOS::HexLiteral(uint64_t value) {
  // The cast is because some platforms define uint64_t as unsigned long long,
  // while others (e.g. z/OS) define it as unsigned long.
  return fprintf(fp_, "%.16lx", static_cast<unsigned long>(value));
}

void PlatformEmbeddedFileWriterZOS::FilePrologue() {
  fprintf(fp_,
          "&C SETC 'embed'\n"
          " SYSSTATE AMODE64=YES\n"
          "&C csect\n"
          "&C amode 64\n"
          "&C rmode 64\n");
}

void PlatformEmbeddedFileWriterZOS::DeclareExternalFilename(
    int fileid, const char* filename) {
  // Not used.
}

void PlatformEmbeddedFileWriterZOS::FileEpilogue() { fprintf(fp_, " end\n"); }

int PlatformEmbeddedFileWriterZOS::IndentedDataDirective(
    DataDirective directive) {
  // Not used.
  return 0;
}

DataDirective PlatformEmbeddedFileWriterZOS::ByteChunkDataDirective() const {
  return kQuad;
}

int PlatformEmbeddedFileWriterZOS::WriteByteChunk(const uint8_t* data) {
  DCHECK_EQ(ByteChunkDataDirective(), kQuad);
  const uint64_t* quad_ptr = reinterpret_cast<const uint64_t*>(data);
  return HexLiteral(*quad_ptr);
}

void PlatformEmbeddedFileWriterZOS::SectionText() {
  // Not used.
}

void PlatformEmbeddedFileWriterZOS::SectionRoData() {
  // Not used.
}

}  // namespace internal
}  // namespace v8
