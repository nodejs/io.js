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

// Allow dynamic symbol lookup in an in-memory Elf image.
//

#include "absl/debugging/internal/elf_mem_image.h"

#ifdef ABSL_HAVE_ELF_MEM_IMAGE  // defined in elf_mem_image.h

#include <string.h>
#include <cassert>
#include <cstddef>
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"

// From binutils/include/elf/common.h (this doesn't appear to be documented
// anywhere else).
//
//   /* This flag appears in a Versym structure.  It means that the symbol
//      is hidden, and is only visible with an explicit version number.
//      This is a GNU extension.  */
//   #define VERSYM_HIDDEN           0x8000
//
//   /* This is the mask for the rest of the Versym information.  */
//   #define VERSYM_VERSION          0x7fff

#define VERSYM_VERSION 0x7fff

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

namespace {

#if __SIZEOF_POINTER__ == 4
const int kElfClass = ELFCLASS32;
int ElfBind(const ElfW(Sym) *symbol) { return ELF32_ST_BIND(symbol->st_info); }
int ElfType(const ElfW(Sym) *symbol) { return ELF32_ST_TYPE(symbol->st_info); }
#elif __SIZEOF_POINTER__ == 8
const int kElfClass = ELFCLASS64;
int ElfBind(const ElfW(Sym) *symbol) { return ELF64_ST_BIND(symbol->st_info); }
int ElfType(const ElfW(Sym) *symbol) { return ELF64_ST_TYPE(symbol->st_info); }
#else
const int kElfClass = -1;
int ElfBind(const ElfW(Sym) *) {
  ABSL_RAW_LOG(FATAL, "Unexpected word size");
  return 0;
}
int ElfType(const ElfW(Sym) *) {
  ABSL_RAW_LOG(FATAL, "Unexpected word size");
  return 0;
}
#endif

// Extract an element from one of the ELF tables, cast it to desired type.
// This is just a simple arithmetic and a glorified cast.
// Callers are responsible for bounds checking.
template <typename T>
const T *GetTableElement(const ElfW(Ehdr) * ehdr, ElfW(Off) table_offset,
                         ElfW(Word) element_size, size_t index) {
  return reinterpret_cast<const T*>(reinterpret_cast<const char *>(ehdr)
                                    + table_offset
                                    + index * element_size);
}

}  // namespace

// The value of this variable doesn't matter; it's used only for its
// unique address.
const int ElfMemImage::kInvalidBaseSentinel = 0;

ElfMemImage::ElfMemImage(const void *base) {
  ABSL_RAW_CHECK(base != kInvalidBase, "bad pointer");
  Init(base);
}

int ElfMemImage::GetNumSymbols() const {
  if (!hash_) {
    return 0;
  }
  // See http://www.caldera.com/developers/gabi/latest/ch5.dynamic.html#hash
  return static_cast<int>(hash_[1]);
}

const ElfW(Sym) *ElfMemImage::GetDynsym(int index) const {
  ABSL_RAW_CHECK(index < GetNumSymbols(), "index out of range");
  return dynsym_ + index;
}

const ElfW(Versym) *ElfMemImage::GetVersym(int index) const {
  ABSL_RAW_CHECK(index < GetNumSymbols(), "index out of range");
  return versym_ + index;
}

const ElfW(Phdr) *ElfMemImage::GetPhdr(int index) const {
  ABSL_RAW_CHECK(index >= 0 && index < ehdr_->e_phnum, "index out of range");
  return GetTableElement<ElfW(Phdr)>(ehdr_, ehdr_->e_phoff, ehdr_->e_phentsize,
                                     static_cast<size_t>(index));
}

const char *ElfMemImage::GetDynstr(ElfW(Word) offset) const {
  ABSL_RAW_CHECK(offset < strsize_, "offset out of range");
  return dynstr_ + offset;
}

const void *ElfMemImage::GetSymAddr(const ElfW(Sym) *sym) const {
  if (sym->st_shndx == SHN_UNDEF || sym->st_shndx >= SHN_LORESERVE) {
    // Symbol corresponds to "special" (e.g. SHN_ABS) section.
    return reinterpret_cast<const void *>(sym->st_value);
  }
  ABSL_RAW_CHECK(link_base_ < sym->st_value, "symbol out of range");
  return GetTableElement<char>(ehdr_, 0, 1, sym->st_value - link_base_);
}

const ElfW(Verdef) *ElfMemImage::GetVerdef(int index) const {
  ABSL_RAW_CHECK(0 <= index && static_cast<size_t>(index) <= verdefnum_,
                 "index out of range");
  const ElfW(Verdef) *version_definition = verdef_;
  while (version_definition->vd_ndx < index && version_definition->vd_next) {
    const char *const version_definition_as_char =
        reinterpret_cast<const char *>(version_definition);
    version_definition =
        reinterpret_cast<const ElfW(Verdef) *>(version_definition_as_char +
                                               version_definition->vd_next);
  }
  return version_definition->vd_ndx == index ? version_definition : nullptr;
}

const ElfW(Verdaux) *ElfMemImage::GetVerdefAux(
    const ElfW(Verdef) *verdef) const {
  return reinterpret_cast<const ElfW(Verdaux) *>(verdef+1);
}

const char *ElfMemImage::GetVerstr(ElfW(Word) offset) const {
  ABSL_RAW_CHECK(offset < strsize_, "offset out of range");
  return dynstr_ + offset;
}

void ElfMemImage::Init(const void *base) {
  ehdr_      = nullptr;
  dynsym_    = nullptr;
  dynstr_    = nullptr;
  versym_    = nullptr;
  verdef_    = nullptr;
  hash_      = nullptr;
  strsize_   = 0;
  verdefnum_ = 0;
  // Sentinel: PT_LOAD .p_vaddr can't possibly be this.
  link_base_ = ~ElfW(Addr){0};  // NOLINT(readability/braces)
  if (!base) {
    return;
  }
  const char *const base_as_char = reinterpret_cast<const char *>(base);
  if (base_as_char[EI_MAG0] != ELFMAG0 || base_as_char[EI_MAG1] != ELFMAG1 ||
      base_as_char[EI_MAG2] != ELFMAG2 || base_as_char[EI_MAG3] != ELFMAG3) {
    assert(false);
    return;
  }
  int elf_class = base_as_char[EI_CLASS];
  if (elf_class != kElfClass) {
    assert(false);
    return;
  }
  switch (base_as_char[EI_DATA]) {
    case ELFDATA2LSB: {
#ifndef ABSL_IS_LITTLE_ENDIAN
      assert(false);
      return;
#endif
      break;
    }
    case ELFDATA2MSB: {
#ifndef ABSL_IS_BIG_ENDIAN
      assert(false);
      return;
#endif
      break;
    }
    default: {
      assert(false);
      return;
    }
  }

  ehdr_ = reinterpret_cast<const ElfW(Ehdr) *>(base);
  const ElfW(Phdr) *dynamic_program_header = nullptr;
  for (int i = 0; i < ehdr_->e_phnum; ++i) {
    const ElfW(Phdr) *const program_header = GetPhdr(i);
    switch (program_header->p_type) {
      case PT_LOAD:
        if (!~link_base_) {
          link_base_ = program_header->p_vaddr;
        }
        break;
      case PT_DYNAMIC:
        dynamic_program_header = program_header;
        break;
    }
  }
  if (!~link_base_ || !dynamic_program_header) {
    assert(false);
    // Mark this image as not present. Can not recur infinitely.
    Init(nullptr);
    return;
  }
  ptrdiff_t relocation =
      base_as_char - reinterpret_cast<const char *>(link_base_);
  ElfW(Dyn)* dynamic_entry = reinterpret_cast<ElfW(Dyn)*>(
      static_cast<intptr_t>(dynamic_program_header->p_vaddr) + relocation);
  for (; dynamic_entry->d_tag != DT_NULL; ++dynamic_entry) {
    const auto value =
        static_cast<intptr_t>(dynamic_entry->d_un.d_val) + relocation;
    switch (dynamic_entry->d_tag) {
      case DT_HASH:
        hash_ = reinterpret_cast<ElfW(Word) *>(value);
        break;
      case DT_SYMTAB:
        dynsym_ = reinterpret_cast<ElfW(Sym) *>(value);
        break;
      case DT_STRTAB:
        dynstr_ = reinterpret_cast<const char *>(value);
        break;
      case DT_VERSYM:
        versym_ = reinterpret_cast<ElfW(Versym) *>(value);
        break;
      case DT_VERDEF:
        verdef_ = reinterpret_cast<ElfW(Verdef) *>(value);
        break;
      case DT_VERDEFNUM:
        verdefnum_ = static_cast<size_t>(dynamic_entry->d_un.d_val);
        break;
      case DT_STRSZ:
        strsize_ = static_cast<size_t>(dynamic_entry->d_un.d_val);
        break;
      default:
        // Unrecognized entries explicitly ignored.
        break;
    }
  }
  if (!hash_ || !dynsym_ || !dynstr_ || !versym_ ||
      !verdef_ || !verdefnum_ || !strsize_) {
    assert(false);  // invalid VDSO
    // Mark this image as not present. Can not recur infinitely.
    Init(nullptr);
    return;
  }
}

bool ElfMemImage::LookupSymbol(const char *name,
                               const char *version,
                               int type,
                               SymbolInfo *info_out) const {
  for (const SymbolInfo& info : *this) {
    if (strcmp(info.name, name) == 0 && strcmp(info.version, version) == 0 &&
        ElfType(info.symbol) == type) {
      if (info_out) {
        *info_out = info;
      }
      return true;
    }
  }
  return false;
}

bool ElfMemImage::LookupSymbolByAddress(const void *address,
                                        SymbolInfo *info_out) const {
  for (const SymbolInfo& info : *this) {
    const char *const symbol_start =
        reinterpret_cast<const char *>(info.address);
    const char *const symbol_end = symbol_start + info.symbol->st_size;
    if (symbol_start <= address && address < symbol_end) {
      if (info_out) {
        // Client wants to know details for that symbol (the usual case).
        if (ElfBind(info.symbol) == STB_GLOBAL) {
          // Strong symbol; just return it.
          *info_out = info;
          return true;
        } else {
          // Weak or local. Record it, but keep looking for a strong one.
          *info_out = info;
        }
      } else {
        // Client only cares if there is an overlapping symbol.
        return true;
      }
    }
  }
  return false;
}

ElfMemImage::SymbolIterator::SymbolIterator(const void *const image, int index)
    : index_(index), image_(image) {
}

const ElfMemImage::SymbolInfo *ElfMemImage::SymbolIterator::operator->() const {
  return &info_;
}

const ElfMemImage::SymbolInfo& ElfMemImage::SymbolIterator::operator*() const {
  return info_;
}

bool ElfMemImage::SymbolIterator::operator==(const SymbolIterator &rhs) const {
  return this->image_ == rhs.image_ && this->index_ == rhs.index_;
}

bool ElfMemImage::SymbolIterator::operator!=(const SymbolIterator &rhs) const {
  return !(*this == rhs);
}

ElfMemImage::SymbolIterator &ElfMemImage::SymbolIterator::operator++() {
  this->Update(1);
  return *this;
}

ElfMemImage::SymbolIterator ElfMemImage::begin() const {
  SymbolIterator it(this, 0);
  it.Update(0);
  return it;
}

ElfMemImage::SymbolIterator ElfMemImage::end() const {
  return SymbolIterator(this, GetNumSymbols());
}

void ElfMemImage::SymbolIterator::Update(int increment) {
  const ElfMemImage *image = reinterpret_cast<const ElfMemImage *>(image_);
  ABSL_RAW_CHECK(image->IsPresent() || increment == 0, "");
  if (!image->IsPresent()) {
    return;
  }
  index_ += increment;
  if (index_ >= image->GetNumSymbols()) {
    index_ = image->GetNumSymbols();
    return;
  }
  const ElfW(Sym)    *symbol = image->GetDynsym(index_);
  const ElfW(Versym) *version_symbol = image->GetVersym(index_);
  ABSL_RAW_CHECK(symbol && version_symbol, "");
  const char *const symbol_name = image->GetDynstr(symbol->st_name);
#if defined(__NetBSD__)
  const int version_index = version_symbol->vs_vers & VERSYM_VERSION;
#else
  const ElfW(Versym) version_index = version_symbol[0] & VERSYM_VERSION;
#endif
  const ElfW(Verdef) *version_definition = nullptr;
  const char *version_name = "";
  if (symbol->st_shndx == SHN_UNDEF) {
    // Undefined symbols reference DT_VERNEED, not DT_VERDEF, and
    // version_index could well be greater than verdefnum_, so calling
    // GetVerdef(version_index) may trigger assertion.
  } else {
    version_definition = image->GetVerdef(version_index);
  }
  if (version_definition) {
    // I am expecting 1 or 2 auxiliary entries: 1 for the version itself,
    // optional 2nd if the version has a parent.
    ABSL_RAW_CHECK(
        version_definition->vd_cnt == 1 || version_definition->vd_cnt == 2,
        "wrong number of entries");
    const ElfW(Verdaux) *version_aux = image->GetVerdefAux(version_definition);
    version_name = image->GetVerstr(version_aux->vda_name);
  }
  info_.name    = symbol_name;
  info_.version = version_name;
  info_.address = image->GetSymAddr(symbol);
  info_.symbol  = symbol;
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HAVE_ELF_MEM_IMAGE
