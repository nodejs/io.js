// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/gdb-jit.h"

#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "include/v8-callbacks.h"
#include "src/api/api-inl.h"
#include "src/base/address-region.h"
#include "src/base/bits.h"
#include "src/base/hashmap.h"
#include "src/base/memory.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/execution/frames-inl.h"
#include "src/execution/frames.h"
#include "src/handles/global-handles.h"
#include "src/init/bootstrapper.h"
#include "src/objects/code-inl.h"
#include "src/objects/objects.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-chunk-list.h"

namespace v8 {
namespace internal {
namespace GDBJITInterface {

#ifdef ENABLE_GDB_JIT_INTERFACE

#ifdef __APPLE__
#define __MACH_O
class MachO;
class MachOSection;
using DebugObject = MachO;
using DebugSection = MachOSection;
#else
#define __ELF
class ELF;
class ELFSection;
using DebugObject = ELF;
using DebugSection = ELFSection;
#endif

class Writer {
 public:
  explicit Writer(DebugObject* debug_object)
      : debug_object_(debug_object),
        position_(0),
        capacity_(1024),
        buffer_(reinterpret_cast<uint8_t*>(base::Malloc(capacity_))) {}

  ~Writer() { base::Free(buffer_); }

  uintptr_t position() const { return position_; }

  template <typename T>
  class Slot {
   public:
    Slot(Writer* w, uintptr_t offset) : w_(w), offset_(offset) {}

    T* operator->() { return w_->RawSlotAt<T>(offset_); }

    void set(const T& value) {
      base::WriteUnalignedValue(w_->AddressAt<T>(offset_), value);
    }

    Slot<T> at(int i) { return Slot<T>(w_, offset_ + sizeof(T) * i); }

   private:
    Writer* w_;
    uintptr_t offset_;
  };

  template <typename T>
  void Write(const T& val) {
    Ensure(position_ + sizeof(T));
    base::WriteUnalignedValue(AddressAt<T>(position_), val);
    position_ += sizeof(T);
  }

  template <typename T>
  Slot<T> SlotAt(uintptr_t offset) {
    Ensure(offset + sizeof(T));
    return Slot<T>(this, offset);
  }

  template <typename T>
  Slot<T> CreateSlotHere() {
    return CreateSlotsHere<T>(1);
  }

  template <typename T>
  Slot<T> CreateSlotsHere(uint32_t count) {
    uintptr_t slot_position = position_;
    position_ += sizeof(T) * count;
    Ensure(position_);
    return SlotAt<T>(slot_position);
  }

  void Ensure(uintptr_t pos) {
    if (capacity_ < pos) {
      while (capacity_ < pos) capacity_ *= 2;
      buffer_ = reinterpret_cast<uint8_t*>(base::Realloc(buffer_, capacity_));
    }
  }

  DebugObject* debug_object() { return debug_object_; }

  uint8_t* buffer() { return buffer_; }

  void Align(uintptr_t align) {
    uintptr_t delta = position_ % align;
    if (delta == 0) return;
    uintptr_t padding = align - delta;
    Ensure(position_ += padding);
    DCHECK_EQ(position_ % align, 0);
  }

  void WriteULEB128(uintptr_t value) {
    do {
      uint8_t byte = value & 0x7F;
      value >>= 7;
      if (value != 0) byte |= 0x80;
      Write<uint8_t>(byte);
    } while (value != 0);
  }

  void WriteSLEB128(intptr_t value) {
    bool more = true;
    while (more) {
      int8_t byte = value & 0x7F;
      bool byte_sign = byte & 0x40;
      value >>= 7;

      if ((value == 0 && !byte_sign) || (value == -1 && byte_sign)) {
        more = false;
      } else {
        byte |= 0x80;
      }

      Write<int8_t>(byte);
    }
  }

  void WriteString(const char* str) {
    do {
      Write<char>(*str);
    } while (*str++);
  }

 private:
  template <typename T>
  friend class Slot;

  template <typename T>
  Address AddressAt(uintptr_t offset) {
    DCHECK(offset < capacity_ && offset + sizeof(T) <= capacity_);
    return reinterpret_cast<Address>(&buffer_[offset]);
  }

  template <typename T>
  T* RawSlotAt(uintptr_t offset) {
    DCHECK(offset < capacity_ && offset + sizeof(T) <= capacity_);
    return reinterpret_cast<T*>(&buffer_[offset]);
  }

  DebugObject* debug_object_;
  uintptr_t position_;
  uintptr_t capacity_;
  uint8_t* buffer_;
};

class ELFStringTable;

template <typename THeader>
class DebugSectionBase : public ZoneObject {
 public:
  virtual ~DebugSectionBase() = default;

  virtual void WriteBody(Writer::Slot<THeader> header, Writer* writer) {
    uintptr_t start = writer->position();
    if (WriteBodyInternal(writer)) {
      uintptr_t end = writer->position();
      header->offset = static_cast<uint32_t>(start);
#if defined(__MACH_O)
      header->addr = 0;
#endif
      header->size = end - start;
    }
  }

  virtual bool WriteBodyInternal(Writer* writer) { return false; }

  using Header = THeader;
};

struct MachOSectionHeader {
  char sectname[16];
  char segname[16];
#if V8_TARGET_ARCH_IA32
  uint32_t addr;
  uint32_t size;
#else
  uint64_t addr;
  uint64_t size;
#endif
  uint32_t offset;
  uint32_t align;
  uint32_t reloff;
  uint32_t nreloc;
  uint32_t flags;
  uint32_t reserved1;
  uint32_t reserved2;
};

class MachOSection : public DebugSectionBase<MachOSectionHeader> {
 public:
  enum Type {
    S_REGULAR = 0x0u,
    S_ATTR_COALESCED = 0xBu,
    S_ATTR_SOME_INSTRUCTIONS = 0x400u,
    S_ATTR_DEBUG = 0x02000000u,
    S_ATTR_PURE_INSTRUCTIONS = 0x80000000u
  };

  MachOSection(const char* name, const char* segment, uint32_t align,
               uint32_t flags)
      : name_(name), segment_(segment), align_(align), flags_(flags) {
    if (align_ != 0) {
      DCHECK(base::bits::IsPowerOfTwo(align));
      align_ = base::bits::WhichPowerOfTwo(align_);
    }
  }

  ~MachOSection() override = default;

  virtual void PopulateHeader(Writer::Slot<Header> header) {
    header->addr = 0;
    header->size = 0;
    header->offset = 0;
    header->align = align_;
    header->reloff = 0;
    header->nreloc = 0;
    header->flags = flags_;
    header->reserved1 = 0;
    header->reserved2 = 0;
    memset(header->sectname, 0, sizeof(header->sectname));
    memset(header->segname, 0, sizeof(header->segname));
    DCHECK(strlen(name_) < sizeof(header->sectname));
    DCHECK(strlen(segment_) < sizeof(header->segname));
    strncpy(header->sectname, name_, sizeof(header->sectname));
    strncpy(header->segname, segment_, sizeof(header->segname));
  }

 private:
  const char* name_;
  const char* segment_;
  uint32_t align_;
  uint32_t flags_;
};

struct ELFSectionHeader {
  uint32_t name;
  uint32_t type;
  uintptr_t flags;
  uintptr_t address;
  uintptr_t offset;
  uintptr_t size;
  uint32_t link;
  uint32_t info;
  uintptr_t alignment;
  uintptr_t entry_size;
};

#if defined(__ELF)
class ELFSection : public DebugSectionBase<ELFSectionHeader> {
 public:
  enum Type {
    TYPE_NULL = 0,
    TYPE_PROGBITS = 1,
    TYPE_SYMTAB = 2,
    TYPE_STRTAB = 3,
    TYPE_RELA = 4,
    TYPE_HASH = 5,
    TYPE_DYNAMIC = 6,
    TYPE_NOTE = 7,
    TYPE_NOBITS = 8,
    TYPE_REL = 9,
    TYPE_SHLIB = 10,
    TYPE_DYNSYM = 11,
    TYPE_LOPROC = 0x70000000,
    TYPE_X86_64_UNWIND = 0x70000001,
    TYPE_HIPROC = 0x7FFFFFFF,
    TYPE_LOUSER = 0x80000000,
    TYPE_HIUSER = 0xFFFFFFFF
  };

  enum Flags { FLAG_WRITE = 1, FLAG_ALLOC = 2, FLAG_EXEC = 4 };

  enum SpecialIndexes { INDEX_ABSOLUTE = 0xFFF1 };

  ELFSection(const char* name, Type type, uintptr_t align)
      : name_(name), type_(type), align_(align) {}

  ~ELFSection() override = default;

  void PopulateHeader(Writer::Slot<Header> header, ELFStringTable* strtab);

  void WriteBody(Writer::Slot<Header> header, Writer* w) override {
    uintptr_t start = w->position();
    if (WriteBodyInternal(w)) {
      uintptr_t end = w->position();
      header->offset = start;
      header->size = end - start;
    }
  }

  bool WriteBodyInternal(Writer* w) override { return false; }

  uint16_t index() const { return index_; }
  void set_index(uint16_t index) { index_ = index; }

 protected:
  virtual void PopulateHeader(Writer::Slot<Header> header) {
    header->flags = 0;
    header->address = 0;
    header->offset = 0;
    header->size = 0;
    header->link = 0;
    header->info = 0;
    header->entry_size = 0;
  }

 private:
  const char* name_;
  Type type_;
  uintptr_t align_;
  uint16_t index_;
};
#endif  // defined(__ELF)

#if defined(__MACH_O)
class MachOTextSection : public MachOSection {
 public:
  MachOTextSection(uint32_t align, uintptr_t addr, uintptr_t size)
      : MachOSection("__text", "__TEXT", align,
                     MachOSection::S_REGULAR |
                         MachOSection::S_ATTR_SOME_INSTRUCTIONS |
                         MachOSection::S_ATTR_PURE_INSTRUCTIONS),
        addr_(addr),
        size_(size) {}

 protected:
  virtual void PopulateHeader(Writer::Slot<Header> header) {
    MachOSection::PopulateHeader(header);
    header->addr = addr_;
    header->size = size_;
  }

 private:
  uintptr_t addr_;
  uintptr_t size_;
};
#endif  // defined(__MACH_O)

#if defined(__ELF)
class FullHeaderELFSection : public ELFSection {
 public:
  FullHeaderELFSection(const char* name, Type type, uintptr_t align,
                       uintptr_t addr, uintptr_t offset, uintptr_t size,
                       uintptr_t flags)
      : ELFSection(name, type, align),
        addr_(addr),
        offset_(offset),
        size_(size),
        flags_(flags) {}

 protected:
  void PopulateHeader(Writer::Slot<Header> header) override {
    ELFSection::PopulateHeader(header);
    header->address = addr_;
    header->offset = offset_;
    header->size = size_;
    header->flags = flags_;
  }

 private:
  uintptr_t addr_;
  uintptr_t offset_;
  uintptr_t size_;
  uintptr_t flags_;
};

class ELFStringTable : public ELFSection {
 public:
  explicit ELFStringTable(const char* name)
      : ELFSection(name, TYPE_STRTAB, 1),
        writer_(nullptr),
        offset_(0),
        size_(0) {}

  uintptr_t Add(const char* str) {
    if (*str == '\0') return 0;

    uintptr_t offset = size_;
    WriteString(str);
    return offset;
  }

  void AttachWriter(Writer* w) {
    writer_ = w;
    offset_ = writer_->position();

    // First entry in the string table should be an empty string.
    WriteString("");
  }

  void DetachWriter() { writer_ = nullptr; }

  void WriteBody(Writer::Slot<Header> header, Writer* w) override {
    DCHECK_NULL(writer_);
    header->offset = offset_;
    header->size = size_;
  }

 private:
  void WriteString(const char* str) {
    uintptr_t written = 0;
    do {
      writer_->Write(*str);
      written++;
    } while (*str++);
    size_ += written;
  }

  Writer* writer_;

  uintptr_t offset_;
  uintptr_t size_;
};

void ELFSection::PopulateHeader(Writer::Slot<ELFSection::Header> header,
                                ELFStringTable* strtab) {
  header->name = static_cast<uint32_t>(strtab->Add(name_));
  header->type = type_;
  header->alignment = align_;
  PopulateHeader(header);
}
#endif  // defined(__ELF)

#if defined(__MACH_O)
class MachO {
 public:
  explicit MachO(Zone* zone) : sections_(zone) {}

  size_t AddSection(MachOSection* section) {
    sections_.push_back(section);
    return sections_.size() - 1;
  }

  void Write(Writer* w, uintptr_t code_start, uintptr_t code_size) {
    Writer::Slot<MachOHeader> header = WriteHeader(w);
    uintptr_t load_command_start = w->position();
    Writer::Slot<MachOSegmentCommand> cmd =
        WriteSegmentCommand(w, code_start, code_size);
    WriteSections(w, cmd, header, load_command_start);
  }

 private:
  struct MachOHeader {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
#if V8_TARGET_ARCH_X64
    uint32_t reserved;
#endif
  };

  struct MachOSegmentCommand {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
#if V8_TARGET_ARCH_IA32
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
#else
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
#endif
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
  };

  enum MachOLoadCommandCmd {
    LC_SEGMENT_32 = 0x00000001u,
    LC_SEGMENT_64 = 0x00000019u
  };

  Writer::Slot<MachOHeader> WriteHeader(Writer* w) {
    DCHECK_EQ(w->position(), 0);
    Writer::Slot<MachOHeader> header = w->CreateSlotHere<MachOHeader>();
#if V8_TARGET_ARCH_IA32
    header->magic = 0xFEEDFACEu;
    header->cputype = 7;     // i386
    header->cpusubtype = 3;  // CPU_SUBTYPE_I386_ALL
#elif V8_TARGET_ARCH_X64
    header->magic = 0xFEEDFACFu;
    header->cputype = 7 | 0x01000000;  // i386 | 64-bit ABI
    header->cpusubtype = 3;            // CPU_SUBTYPE_I386_ALL
    header->reserved = 0;
#else
#error Unsupported target architecture.
#endif
    header->filetype = 0x1;  // MH_OBJECT
    header->ncmds = 1;
    header->sizeofcmds = 0;
    header->flags = 0;
    return header;
  }

  Writer::Slot<MachOSegmentCommand> WriteSegmentCommand(Writer* w,
                                                        uintptr_t code_start,
                                                        uintptr_t code_size) {
    Writer::Slot<MachOSegmentCommand> cmd =
        w->CreateSlotHere<MachOSegmentCommand>();
#if V8_TARGET_ARCH_IA32
    cmd->cmd = LC_SEGMENT_32;
#else
    cmd->cmd = LC_SEGMENT_64;
#endif
    cmd->vmaddr = code_start;
    cmd->vmsize = code_size;
    cmd->fileoff = 0;
    cmd->filesize = 0;
    cmd->maxprot = 7;
    cmd->initprot = 7;
    cmd->flags = 0;
    cmd->nsects = static_cast<uint32_t>(sections_.size());
    memset(cmd->segname, 0, 16);
    cmd->cmdsize = sizeof(MachOSegmentCommand) +
                   sizeof(MachOSection::Header) * cmd->nsects;
    return cmd;
  }

  void WriteSections(Writer* w, Writer::Slot<MachOSegmentCommand> cmd,
                     Writer::Slot<MachOHeader> header,
                     uintptr_t load_command_start) {
    Writer::Slot<MachOSection::Header> headers =
        w->CreateSlotsHere<MachOSection::Header>(
            static_cast<uint32_t>(sections_.size()));
    cmd->fileoff = w->position();
    header->sizeofcmds =
        static_cast<uint32_t>(w->position() - load_command_start);
    uint32_t index = 0;
    for (MachOSection* section : sections_) {
      section->PopulateHeader(headers.at(index));
      section->WriteBody(headers.at(index), w);
      index++;
    }
    cmd->filesize = w->position() - (uintptr_t)cmd->fileoff;
  }

  ZoneChunkList<MachOSection*> sections_;
};
#endif  // defined(__MACH_O)

#if defined(__ELF)
class ELF {
 public:
  explicit ELF(Zone* zone) : sections_(zone) {
    sections_.push_back(zone->New<ELFSection>("", ELFSection::TYPE_NULL, 0));
    sections_.push_back(zone->New<ELFStringTable>(".shstrtab"));
  }

  void Write(Writer* w) {
    WriteHeader(w);
    WriteSectionTable(w);
    WriteSections(w);
  }

  ELFSection* SectionAt(uint32_t index) { return *sections_.Find(index); }

  size_t AddSection(ELFSection* section) {
    sections_.push_back(section);
    section->set_index(sections_.size() - 1);
    return sections_.size() - 1;
  }

 private:
  struct ELFHeader {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uintptr_t entry;
    uintptr_t pht_offset;
    uintptr_t sht_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t pht_entry_size;
    uint16_t pht_entry_num;
    uint16_t sht_entry_size;
    uint16_t sht_entry_num;
    uint16_t sht_strtab_index;
  };

  void WriteHeader(Writer* w) {
    DCHECK_EQ(w->position(), 0);
    Writer::Slot<ELFHeader> header = w->CreateSlotHere<ELFHeader>();
#if (V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_ARM)
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1, 0,
                               0,    0,   0,   0,   0, 0, 0, 0};
#elif V8_TARGET_ARCH_X64 && V8_TARGET_ARCH_64_BIT || \
    V8_TARGET_ARCH_PPC64 && V8_TARGET_LITTLE_ENDIAN
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0,
                               0,    0,   0,   0,   0, 0, 0, 0};
#elif V8_TARGET_ARCH_S390X
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 2, 2, 1, 3,
                               0,    0,   0,   0,   0, 0, 0, 0};
#else
#error Unsupported target architecture.
#endif
    memcpy(header->ident, ident, 16);
    header->type = 1;
#if V8_TARGET_ARCH_IA32
    header->machine = 3;
#elif V8_TARGET_ARCH_X64
    // Processor identification value for x64 is 62 as defined in
    //    System V ABI, AMD64 Supplement
    //    http://www.x86-64.org/documentation/abi.pdf
    header->machine = 62;
#elif V8_TARGET_ARCH_ARM
    // Set to EM_ARM, defined as 40, in "ARM ELF File Format" at
    // infocenter.arm.com/help/topic/com.arm.doc.dui0101a/DUI0101A_Elf.pdf
    header->machine = 40;
#elif V8_TARGET_ARCH_PPC64 && V8_OS_LINUX
    // Set to EM_PPC64, defined as 21, in Power ABI,
    // Join the next 4 lines, omitting the spaces and double-slashes.
    // https://www-03.ibm.com/technologyconnect/tgcm/TGCMFileServlet.wss/
    // ABI64BitOpenPOWERv1.1_16July2015_pub.pdf?
    // id=B81AEC1A37F5DAF185257C3E004E8845&linkid=1n0000&c_t=
    // c9xw7v5dzsj7gt1ifgf4cjbcnskqptmr
    header->machine = 21;
#elif V8_TARGET_ARCH_S390X
    // Processor identification value is 22 (EM_S390) as defined in the ABI:
    // http://refspecs.linuxbase.org/ELF/zSeries/lzsabi0_s390.html#AEN1691
    // http://refspecs.linuxbase.org/ELF/zSeries/lzsabi0_zSeries.html#AEN1599
    header->machine = 22;
#else
#error Unsupported target architecture.
#endif
    header->version = 1;
    header->entry = 0;
    header->pht_offset = 0;
    header->sht_offset = sizeof(ELFHeader);  // Section table follows header.
    header->flags = 0;
    header->header_size = sizeof(ELFHeader);
    header->pht_entry_size = 0;
    header->pht_entry_num = 0;
    header->sht_entry_size = sizeof(ELFSection::Header);
    header->sht_entry_num = sections_.size();
    header->sht_strtab_index = 1;
  }

  void WriteSectionTable(Writer* w) {
    // Section headers table immediately follows file header.
    DCHECK(w->position() == sizeof(ELFHeader));

    Writer::Slot<ELFSection::Header> headers =
        w->CreateSlotsHere<ELFSection::Header>(
            static_cast<uint32_t>(sections_.size()));

    // String table for section table is the first section.
    ELFStringTable* strtab = static_cast<ELFStringTable*>(SectionAt(1));
    strtab->AttachWriter(w);
    uint32_t index = 0;
    for (ELFSection* section : sections_) {
      section->PopulateHeader(headers.at(index), strtab);
      index++;
    }
    strtab->DetachWriter();
  }

  int SectionHeaderPosition(uint32_t section_index) {
    return sizeof(ELFHeader) + sizeof(ELFSection::Header) * section_index;
  }

  void WriteSections(Writer* w) {
    Writer::Slot<ELFSection::Header> headers =
        w->SlotAt<ELFSection::Header>(sizeof(ELFHeader));

    uint32_t index = 0;
    for (ELFSection* section : sections_) {
      section->WriteBody(headers.at(index), w);
      index++;
    }
  }

  ZoneChunkList<ELFSection*> sections_;
};

class ELFSymbol {
 public:
  enum Type {
    TYPE_NOTYPE = 0,
    TYPE_OBJECT = 1,
    TYPE_FUNC = 2,
    TYPE_SECTION = 3,
    TYPE_FILE = 4,
    TYPE_LOPROC = 13,
    TYPE_HIPROC = 15
  };

  enum Binding {
    BIND_LOCAL = 0,
    BIND_GLOBAL = 1,
    BIND_WEAK = 2,
    BIND_LOPROC = 13,
    BIND_HIPROC = 15
  };

  ELFSymbol(const char* name, uintptr_t value, uintptr_t size, Binding binding,
            Type type, uint16_t section)
      : name(name),
        value(value),
        size(size),
        info((binding << 4) | type),
        other(0),
        section(section) {}

  Binding binding() const { return static_cast<Binding>(info >> 4); }
#if (V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_ARM)
  struct SerializedLayout {
    SerializedLayout(uint32_t name, uintptr_t value, uintptr_t size,
                     Binding binding, Type type, uint16_t section)
        : name(name),
          value(value),
          size(size),
          info((binding << 4) | type),
          other(0),
          section(section) {}

    uint32_t name;
    uintptr_t value;
    uintptr_t size;
    uint8_t info;
    uint8_t other;
    uint16_t section;
  };
#elif V8_TARGET_ARCH_X64 && V8_TARGET_ARCH_64_BIT || \
    V8_TARGET_ARCH_PPC64 && V8_OS_LINUX || V8_TARGET_ARCH_S390X
  struct SerializedLayout {
    SerializedLayout(uint32_t name, uintptr_t value, uintptr_t size,
                     Binding binding, Type type, uint16_t section)
        : name(name),
          info((binding << 4) | type),
          other(0),
          section(section),
          value(value),
          size(size) {}

    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section;
    uintptr_t value;
    uintptr_t size;
  };
#endif

  void Write(Writer::Slot<SerializedLayout> s, ELFStringTable* t) const {
    // Convert symbol names from strings to indexes in the string table.
    s->name = static_cast<uint32_t>(t->Add(name));
    s->value = value;
    s->size = size;
    s->info = info;
    s->other = other;
    s->section = section;
  }

 private:
  const char* name;
  uintptr_t value;
  uintptr_t size;
  uint8_t info;
  uint8_t other;
  uint16_t section;
};

class ELFSymbolTable : public ELFSection {
 public:
  ELFSymbolTable(const char* name, Zone* zone)
      : ELFSection(name, TYPE_SYMTAB, sizeof(uintptr_t)),
        locals_(zone),
        globals_(zone) {}

  void WriteBody(Writer::Slot<Header> header, Writer* w) override {
    w->Align(header->alignment);
    size_t total_symbols = locals_.size() + globals_.size() + 1;
    header->offset = w->position();

    Writer::Slot<ELFSymbol::SerializedLayout> symbols =
        w->CreateSlotsHere<ELFSymbol::SerializedLayout>(
            static_cast<uint32_t>(total_symbols));

    header->size = w->position() - header->offset;

    // String table for this symbol table should follow it in the section table.
    ELFStringTable* strtab =
        static_cast<ELFStringTable*>(w->debug_object()->SectionAt(index() + 1));
    strtab->AttachWriter(w);
    symbols.at(0).set(ELFSymbol::SerializedLayout(
        0, 0, 0, ELFSymbol::BIND_LOCAL, ELFSymbol::TYPE_NOTYPE, 0));
    WriteSymbolsList(&locals_, symbols.at(1), strtab);
    WriteSymbolsList(&globals_,
                     symbols.at(static_cast<uint32_t>(locals_.size() + 1)),
                     strtab);
    strtab->DetachWriter();
  }

  void Add(const ELFSymbol& symbol) {
    if (symbol.binding() == ELFSymbol::BIND_LOCAL) {
      locals_.push_back(symbol);
    } else {
      globals_.push_back(symbol);
    }
  }

 protected:
  void PopulateHeader(Writer::Slot<Header> header) override {
    ELFSection::PopulateHeader(header);
    // We are assuming that string table will follow symbol table.
    header->link = index() + 1;
    header->info = static_cast<uint32_t>(locals_.size() + 1);
    header->entry_size = sizeof(ELFSymbol::SerializedLayout);
  }

 private:
  void WriteSymbolsList(const ZoneChunkList<ELFSymbol>* src,
                        Writer::Slot<ELFSymbol::SerializedLayout> dst,
                        ELFStringTable* strtab) {
    int i = 0;
    for (const ELFSymbol& symbol : *src) {
      symbol.Write(dst.at(i++), strtab);
    }
  }

  ZoneChunkList<ELFSymbol> locals_;
  ZoneChunkList<ELFSymbol> globals_;
};
#endif  // defined(__ELF)

class LineInfo : public Malloced {
 public:
  void SetPosition(intptr_t pc, int pos, bool is_statement) {
    AddPCInfo(PCInfo(pc, pos, is_statement));
  }

  struct PCInfo {
    PCInfo(intptr_t pc, int pos, bool is_statement)
        : pc_(pc), pos_(pos), is_statement_(is_statement) {}

    intptr_t pc_;
    int pos_;
    bool is_statement_;
  };

  std::vector<PCInfo>* pc_info() { return &pc_info_; }

 private:
  void AddPCInfo(const PCInfo& pc_info) { pc_info_.push_back(pc_info); }

  std::vector<PCInfo> pc_info_;
};

class CodeDescription {
 public:
#if V8_TARGET_ARCH_X64
  enum StackState {
    POST_RBP_PUSH,
    POST_RBP_SET,
    POST_RBP_POP,
    STACK_STATE_MAX
  };
#endif

  CodeDescription(const char* name, base::AddressRegion region,
                  Tagged<SharedFunctionInfo> shared, LineInfo* lineinfo,
                  bool is_function)
      : name_(name),
        shared_info_(shared),
        lineinfo_(lineinfo),
        is_function_(is_function),
        code_region_(region) {}

  const char* name() const { return name_; }

  LineInfo* lineinfo() const { return lineinfo_; }

  bool is_function() const { return is_function_; }

  bool has_scope_info() const { return !shared_info_.is_null(); }

  Tagged<ScopeInfo> scope_info() const {
    DCHECK(has_scope_info());
    return shared_info_->scope_info();
  }

  uintptr_t CodeStart() const { return code_region_.begin(); }

  uintptr_t CodeEnd() const { return code_region_.end(); }

  uintptr_t CodeSize() const { return code_region_.size(); }

  bool has_script() {
    return !shared_info_.is_null() && IsScript(shared_info_->script());
  }

  Tagged<Script> script() { return Cast<Script>(shared_info_->script()); }

  bool IsLineInfoAvailable() { return lineinfo_ != nullptr; }

  base::AddressRegion region() { return code_region_; }

#if V8_TARGET_ARCH_X64
  uintptr_t GetStackStateStartAddress(StackState state) const {
    DCHECK(state < STACK_STATE_MAX);
    return stack_state_start_addresses_[state];
  }

  void SetStackStateStartAddress(StackState state, uintptr_t addr) {
    DCHECK(state < STACK_STATE_MAX);
    stack_state_start_addresses_[state] = addr;
  }
#endif

  std::unique_ptr<char[]> GetFilename() {
    if (!shared_info_.is_null() && IsString(script()->name())) {
      return Cast<String>(script()->name())->ToCString();
    } else {
      std::unique_ptr<char[]> result(new char[1]);
      result[0] = 0;
      return result;
    }
  }

  int GetScriptLineNumber(int pos) {
    if (!shared_info_.is_null()) {
      return script()->GetLineNumber(pos) + 1;
    } else {
      return 0;
    }
  }

 private:
  const char* name_;
  Tagged<SharedFunctionInfo> shared_info_;
  LineInfo* lineinfo_;
  bool is_function_;
  base::AddressRegion code_region_;
#if V8_TARGET_ARCH_X64
  uintptr_t stack_state_start_addresses_[STACK_STATE_MAX];
#endif
};

#if defined(__ELF)
static void CreateSymbolsTable(CodeDescription* desc, Zone* zone, ELF* elf,
                               size_t text_section_index) {
  ELFSymbolTable* symtab = zone->New<ELFSymbolTable>(".symtab", zone);
  ELFStringTable* strtab = zone->New<ELFStringTable>(".strtab");

  // Symbol table should be followed by the linked string table.
  elf->AddSection(symtab);
  elf->AddSection(strtab);

  symtab->Add(ELFSymbol("V8 Code", 0, 0, ELFSymbol::BIND_LOCAL,
                        ELFSymbol::TYPE_FILE, ELFSection::INDEX_ABSOLUTE));

  symtab->Add(ELFSymbol(desc->name(), 0, desc->CodeSize(),
                        ELFSymbol::BIND_GLOBAL, ELFSymbol::TYPE_FUNC,
                        text_section_index));
}
#endif  // defined(__ELF)

class DebugInfoSection : public DebugSection {
 public:
  explicit DebugInfoSection(CodeDescription* desc)
#if defined(__ELF)
      : ELFSection(".debug_info", TYPE_PROGBITS, 1),
#else
      : MachOSection("__debug_info", "__DWARF", 1,
                     MachOSection::S_REGULAR | MachOSection::S_ATTR_DEBUG),
#endif
        desc_(desc) {
  }

  // DWARF2 standard
  enum DWARF2LocationOp {
    DW_OP_reg0 = 0x50,
    DW_OP_reg1 = 0x51,
    DW_OP_reg2 = 0x52,
    DW_OP_reg3 = 0x53,
    DW_OP_reg4 = 0x54,
    DW_OP_reg5 = 0x55,
    DW_OP_reg6 = 0x56,
    DW_OP_reg7 = 0x57,
    DW_OP_reg8 = 0x58,
    DW_OP_reg9 = 0x59,
    DW_OP_reg10 = 0x5A,
    DW_OP_reg11 = 0x5B,
    DW_OP_reg12 = 0x5C,
    DW_OP_reg13 = 0x5D,
    DW_OP_reg14 = 0x5E,
    DW_OP_reg15 = 0x5F,
    DW_OP_reg16 = 0x60,
    DW_OP_reg17 = 0x61,
    DW_OP_reg18 = 0x62,
    DW_OP_reg19 = 0x63,
    DW_OP_reg20 = 0x64,
    DW_OP_reg21 = 0x65,
    DW_OP_reg22 = 0x66,
    DW_OP_reg23 = 0x67,
    DW_OP_reg24 = 0x68,
    DW_OP_reg25 = 0x69,
    DW_OP_reg26 = 0x6A,
    DW_OP_reg27 = 0x6B,
    DW_OP_reg28 = 0x6C,
    DW_OP_reg29 = 0x6D,
    DW_OP_reg30 = 0x6E,
    DW_OP_reg31 = 0x6F,
    DW_OP_fbreg = 0x91  // 1 param: SLEB128 offset
  };

  enum DWARF2Encoding { DW_ATE_ADDRESS = 0x1, DW_ATE_SIGNED = 0x5 };

  bool WriteBodyInternal(Writer* w) override {
    uintptr_t cu_start = w->position();
    Writer::Slot<uint32_t> size = w->CreateSlotHere<uint32_t>();
    uintptr_t start = w->position();
    w->Write<uint16_t>(2);  // DWARF version.
    w->Write<uint32_t>(0);  // Abbreviation table offset.
    w->Write<uint8_t>(sizeof(intptr_t));

    w->WriteULEB128(1);  // Abbreviation code.
    w->WriteString(desc_->GetFilename().get());
    w->Write<intptr_t>(desc_->CodeStart());
    w->Write<intptr_t>(desc_->CodeStart() + desc_->CodeSize());
    w->Write<uint32_t>(0);

    uint32_t ty_offset = static_cast<uint32_t>(w->position() - cu_start);
    w->WriteULEB128(3);
    w->Write<uint8_t>(kSystemPointerSize);
    w->WriteString("v8value");

    if (desc_->has_scope_info()) {
      Tagged<ScopeInfo> scope = desc_->scope_info();
      w->WriteULEB128(2);
      w->WriteString(desc_->name());
      w->Write<intptr_t>(desc_->CodeStart());
      w->Write<intptr_t>(desc_->CodeStart() + desc_->CodeSize());
      Writer::Slot<uint32_t> fb_block_size = w->CreateSlotHere<uint32_t>();
      uintptr_t fb_block_start = w->position();
#if V8_TARGET_ARCH_IA32
      w->Write<uint8_t>(DW_OP_reg5);  // The frame pointer's here on ia32
#elif V8_TARGET_ARCH_X64
      w->Write<uint8_t>(DW_OP_reg6);  // and here on x64.
#elif V8_TARGET_ARCH_ARM
      UNIMPLEMENTED();
#elif V8_TARGET_ARCH_MIPS
      UNIMPLEMENTED();
#elif V8_TARGET_ARCH_MIPS64
      UNIMPLEMENTED();
#elif V8_TARGET_ARCH_LOONG64
      UNIMPLEMENTED();
#elif V8_TARGET_ARCH_PPC64 && V8_OS_LINUX
      w->Write<uint8_t>(DW_OP_reg31);  // The frame pointer is here on PPC64.
#elif V8_TARGET_ARCH_S390X
      w->Write<uint8_t>(DW_OP_reg11);  // The frame pointer's here on S390.
#else
#error Unsupported target architecture.
#endif
      fb_block_size.set(static_cast<uint32_t>(w->position() - fb_block_start));

      int params = scope->ParameterCount();
      int context_slots = scope->ContextLocalCount();
      // The real slot ID is internal_slots + context_slot_id.
      int internal_slots = scope->ContextHeaderLength();
      int current_abbreviation = 4;

      for (int param = 0; param < params; ++param) {
        w->WriteULEB128(current_abbreviation++);
        w->WriteString("param");
        w->Write(std::to_string(param).c_str());
        w->Write<uint32_t>(ty_offset);
        Writer::Slot<uint32_t> block_size = w->CreateSlotHere<uint32_t>();
        uintptr_t block_start = w->position();
        w->Write<uint8_t>(DW_OP_fbreg);
        w->WriteSLEB128(StandardFrameConstants::kFixedFrameSizeAboveFp +
                        kSystemPointerSize * (params - param - 1));
        block_size.set(static_cast<uint32_t>(w->position() - block_start));
      }

      // See contexts.h for more information.
      DCHECK(internal_slots == 2 || internal_slots == 3);
      DCHECK_EQ(Context::SCOPE_INFO_INDEX, 0);
      DCHECK_EQ(Context::PREVIOUS_INDEX, 1);
      DCHECK_EQ(Context::EXTENSION_INDEX, 2);
      w->WriteULEB128(current_abbreviation++);
      w->WriteString(".scope_info");
      w->WriteULEB128(current_abbreviation++);
      w->WriteString(".previous");
      if (internal_slots == 3) {
        w->WriteULEB128(current_abbreviation++);
        w->WriteString(".extension");
      }

      for (int context_slot = 0; context_slot < context_slots; ++context_slot) {
        w->WriteULEB128(current_abbreviation++);
        w->WriteString("context_slot");
        w->Write(std::to_string(context_slot + internal_slots).c_str());
      }

      {
        w->WriteULEB128(current_abbreviation++);
        w->WriteString("__function");
        w->Write<uint32_t>(ty_offset);
        Writer::Slot<uint32_t> block_size = w->CreateSlotHere<uint32_t>();
        uintptr_t block_start = w->position();
        w->Write<uint8_t>(DW_OP_fbreg);
        w->WriteSLEB128(StandardFrameConstants::kFunctionOffset);
        block_size.set(static_cast<uint32_t>(w->position() - block_start));
      }

      {
        w->WriteULEB128(current_abbreviation++);
        w->WriteString("__context");
        w->Write<uint32_t>(ty_offset);
        Writer::Slot<uint32_t> block_size = w->CreateSlotHere<uint32_t>();
        uintptr_t block_start = w->position();
        w->Write<uint8_t>(DW_OP_fbreg);
        w->WriteSLEB128(StandardFrameConstants::kContextOffset);
        block_size.set(static_cast<uint32_t>(w->position() - block_start));
      }

      w->WriteULEB128(0);  // Terminate the sub program.
    }

    w->WriteULEB128(0);  // Terminate the compile unit.
    size.set(static_cast<uint32_t>(w->position() - start));
    return true;
  }

 private:
  CodeDescription* desc_;
};

class DebugAbbrevSection : public DebugSection {
 public:
  explicit DebugAbbrevSection(CodeDescription* desc)
#ifdef __ELF
      : ELFSection(".debug_abbrev", TYPE_PROGBITS, 1),
#else
      : MachOSection("__debug_abbrev", "__DWARF", 1,
                     MachOSection::S_REGULAR | MachOSection::S_ATTR_DEBUG),
#endif
        desc_(desc) {
  }

  // DWARF2 standard, figure 14.
  enum DWARF2Tags {
    DW_TAG_FORMAL_PARAMETER = 0x05,
    DW_TAG_POINTER_TYPE = 0xF,
    DW_TAG_COMPILE_UNIT = 0x11,
    DW_TAG_STRUCTURE_TYPE = 0x13,
    DW_TAG_BASE_TYPE = 0x24,
    DW_TAG_SUBPROGRAM = 0x2E,
    DW_TAG_VARIABLE = 0x34
  };

  // DWARF2 standard, figure 16.
  enum DWARF2ChildrenDetermination { DW_CHILDREN_NO = 0, DW_CHILDREN_YES = 1 };

  // DWARF standard, figure 17.
  enum DWARF2Attribute {
    DW_AT_LOCATION = 0x2,
    DW_AT_NAME = 0x3,
    DW_AT_BYTE_SIZE = 0xB,
    DW_AT_STMT_LIST = 0x10,
    DW_AT_LOW_PC = 0x11,
    DW_AT_HIGH_PC = 0x12,
    DW_AT_ENCODING = 0x3E,
    DW_AT_FRAME_BASE = 0x40,
    DW_AT_TYPE = 0x49
  };

  // DWARF2 standard, figure 19.
  enum DWARF2AttributeForm {
    DW_FORM_ADDR = 0x1,
    DW_FORM_BLOCK4 = 0x4,
    DW_FORM_STRING = 0x8,
    DW_FORM_DATA4 = 0x6,
    DW_FORM_BLOCK = 0x9,
    DW_FORM_DATA1 = 0xB,
    DW_FORM_FLAG = 0xC,
    DW_FORM_REF4 = 0x13
  };

  void WriteVariableAbbreviation(Writer* w, int abbreviation_code,
                                 bool has_value, bool is_parameter) {
    w->WriteULEB128(abbreviation_code);
    w->WriteULEB128(is_parameter ? DW_TAG_FORMAL_PARAMETER : DW_TAG_VARIABLE);
    w->Write<uint8_t>(DW_CHILDREN_NO);
    w->WriteULEB128(DW_AT_NAME);
    w->WriteULEB128(DW_FORM_STRING);
    if (has_value) {
      w->WriteULEB128(DW_AT_TYPE);
      w->WriteULEB128(DW_FORM_REF4);
      w->WriteULEB128(DW_AT_LOCATION);
      w->WriteULEB128(DW_FORM_BLOCK4);
    }
    w->WriteULEB128(0);
    w->WriteULEB128(0);
  }

  bool WriteBodyInternal(Writer* w) override {
    int current_abbreviation = 1;
    bool extra_info = desc_->has_scope_info();
    DCHECK(desc_->IsLineInfoAvailable());
    w->WriteULEB128(current_abbreviation++);
    w->WriteULEB128(DW_TAG_COMPILE_UNIT);
    w->Write<uint8_t>(extra_info ? DW_CHILDREN_YES : DW_CHILDREN_NO);
    w->WriteULEB128(DW_AT_NAME);
    w->WriteULEB128(DW_FORM_STRING);
    w->WriteULEB128(DW_AT_LOW_PC);
    w->WriteULEB128(DW_FORM_ADDR);
    w->WriteULEB128(DW_AT_HIGH_PC);
    w->WriteULEB128(DW_FORM_ADDR);
    w->WriteULEB128(DW_AT_STMT_LIST);
    w->WriteULEB128(DW_FORM_DATA4);
    w->WriteULEB128(0);
    w->WriteULEB128(0);

    if (extra_info) {
      Tagged<ScopeInfo> scope = desc_->scope_info();
      int params = scope->ParameterCount();
      int context_slots = scope->ContextLocalCount();
      // The real slot ID is internal_slots + context_slot_id.
      int internal_slots = Context::MIN_CONTEXT_SLOTS;
      // Total children is params + context_slots + internal_slots + 2
      // (__function and __context).

      // The extra duplication below seems to be necessary to keep
      // gdb from getting upset on OSX.
      w->WriteULEB128(current_abbreviation++);  // Abbreviation code.
      w->WriteULEB128(DW_TAG_SUBPROGRAM);
      w->Write<uint8_t>(DW_CHILDREN_YES);
      w->WriteULEB128(DW_AT_NAME);
      w->WriteULEB128(DW_FORM_STRING);
      w->WriteULEB128(DW_AT_LOW_PC);
      w->WriteULEB128(DW_FORM_ADDR);
      w->WriteULEB128(DW_AT_HIGH_PC);
      w->WriteULEB128(DW_FORM_ADDR);
      w->WriteULEB128(DW_AT_FRAME_BASE);
      w->WriteULEB128(DW_FORM_BLOCK4);
      w->WriteULEB128(0);
      w->WriteULEB128(0);

      w->WriteULEB128(current_abbreviation++);
      w->WriteULEB128(DW_TAG_STRUCTURE_TYPE);
      w->Write<uint8_t>(DW_CHILDREN_NO);
      w->WriteULEB128(DW_AT_BYTE_SIZE);
      w->WriteULEB128(DW_FORM_DATA1);
      w->WriteULEB128(DW_AT_NAME);
      w->WriteULEB128(DW_FORM_STRING);
      w->WriteULEB128(0);
      w->WriteULEB128(0);

      for (int param = 0; param < params; ++param) {
        WriteVariableAbbreviation(w, current_abbreviation++, true, true);
      }

      for (int internal_slot = 0; internal_slot < internal_slots;
           ++internal_slot) {
        WriteVariableAbbreviation(w, current_abbreviation++, false, false);
      }

      for (int context_slot = 0; context_slot < context_slots; ++context_slot) {
        WriteVariableAbbreviation(w, current_abbreviation++, false, false);
      }

      // The function.
      WriteVariableAbbreviation(w, current_abbreviation++, true, false);

      // The context.
      WriteVariableAbbreviation(w, current_abbreviation++, true, false);

      w->WriteULEB128(0);  // Terminate the sibling list.
    }

    w->WriteULEB128(0);  // Terminate the table.
    return true;
  }

 private:
  CodeDescription* desc_;
};

class DebugLineSection : public DebugSection {
 public:
  explicit DebugLineSection(CodeDescription* desc)
#ifdef __ELF
      : ELFSection(".debug_line", TYPE_PROGBITS, 1),
#else
      : MachOSection("__debug_line", "__DWARF", 1,
                     MachOSection::S_REGULAR | MachOSection::S_ATTR_DEBUG),
#endif
        desc_(desc) {
  }

  // DWARF2 standard, figure 34.
  enum DWARF2Opcodes {
    DW_LNS_COPY = 1,
    DW_LNS_ADVANCE_PC = 2,
    DW_LNS_ADVANCE_LINE = 3,
    DW_LNS_SET_FILE = 4,
    DW_LNS_SET_COLUMN = 5,
    DW_LNS_NEGATE_STMT = 6
  };

  // DWARF2 standard, figure 35.
  enum DWARF2ExtendedOpcode {
    DW_LNE_END_SEQUENCE = 1,
    DW_LNE_SET_ADDRESS = 2,
    DW_LNE_DEFINE_FILE = 3
  };

  bool WriteBodyInternal(Writer* w) override {
    // Write prologue.
    Writer::Slot<uint32_t> total_length = w->CreateSlotHere<uint32_t>();
    uintptr_t start = w->position();

    // Used for special opcodes
    const int8_t line_base = 1;
    const uint8_t line_range = 7;
    const int8_t max_line_incr = (line_base + line_range - 1);
    const uint8_t opcode_base = DW_LNS_NEGATE_STMT + 1;

    w->Write<uint16_t>(2);  // Field version.
    Writer::Slot<uint32_t> prologue_length = w->CreateSlotHere<uint32_t>();
    uintptr_t prologue_start = w->position();
    w->Write<uint8_t>(1);            // Field minimum_instruction_length.
    w->Write<uint8_t>(1);            // Field default_is_stmt.
    w->Write<int8_t>(line_base);     // Field line_base.
    w->Write<uint8_t>(line_range);   // Field line_range.
    w->Write<uint8_t>(opcode_base);  // Field opcode_base.
    w->Write<uint8_t>(0);            // DW_LNS_COPY operands count.
    w->Write<uint8_t>(1);            // DW_LNS_ADVANCE_PC operands count.
    w->Write<uint8_t>(1);            // DW_LNS_ADVANCE_LINE operands count.
    w->Write<uint8_t>(1);            // DW_LNS_SET_FILE operands count.
    w->Write<uint8_t>(1);            // DW_LNS_SET_COLUMN operands count.
    w->Write<uint8_t>(0);            // DW_LNS_NEGATE_STMT operands count.
    w->Write<uint8_t>(0);            // Empty include_directories sequence.
    w->WriteString(desc_->GetFilename().get());  // File name.
    w->WriteULEB128(0);                          // Current directory.
    w->WriteULEB128(0);                          // Unknown modification time.
    w->WriteULEB128(0);                          // Unknown file size.
    w->Write<uint8_t>(0);
    prologue_length.set(static_cast<uint32_t>(w->position() - prologue_start));

    WriteExtendedOpcode(w, DW_LNE_SET_ADDRESS, sizeof(intptr_t));
    w->Write<intptr_t>(desc_->CodeStart());
    w->Write<uint8_t>(DW_LNS_COPY);

    intptr_t pc = 0;
    intptr_t line = 1;
    bool is_statement = true;

    std::vector<LineInfo::PCInfo>* pc_info = desc_->lineinfo()->pc_info();
    std::sort(pc_info->begin(), pc_info->end(), &ComparePCInfo);

    for (size_t i = 0; i < pc_info->size(); i++) {
      LineInfo::PCInfo* info = &pc_info->at(i);
      DCHECK(info->pc_ >= pc);

      // Reduce bloating in the debug line table by removing duplicate line
      // entries (per DWARF2 standard).
      intptr_t new_line = desc_->GetScriptLineNumber(info->pos_);
      if (new_line == line) {
        continue;
      }

      // Mark statement boundaries.  For a better debugging experience, mark
      // the last pc address in the function as a statement (e.g. "}"), so that
      // a user can see the result of the last line executed in the function,
      // should control reach the end.
      if ((i + 1) == pc_info->size()) {
        if (!is_statement) {
          w->Write<uint8_t>(DW_LNS_NEGATE_STMT);
        }
      } else if (is_statement != info->is_statement_) {
        w->Write<uint8_t>(DW_LNS_NEGATE_STMT);
        is_statement = !is_statement;
      }

      // Generate special opcodes, if possible.  This results in more compact
      // debug line tables.  See the DWARF 2.0 standard to learn more about
      // special opcodes.
      uintptr_t pc_diff = info->pc_ - pc;
      intptr_t line_diff = new_line - line;

      // Compute special opcode (see DWARF 2.0 standard)
      intptr_t special_opcode =
          (line_diff - line_base) + (line_range * pc_diff) + opcode_base;

      // If special_opcode is less than or equal to 255, it can be used as a
      // special opcode.  If line_diff is larger than the max line increment
      // allowed for a special opcode, or if line_diff is less than the minimum
      // line that can be added to the line register (i.e. line_base), then
      // special_opcode can't be used.
      if ((special_opcode >= opcode_base) && (special_opcode <= 255) &&
          (line_diff <= max_line_incr) && (line_diff >= line_base)) {
        w->Write<uint8_t>(special_opcode);
      } else {
        w->Write<uint8_t>(DW_LNS_ADVANCE_PC);
        w->WriteSLEB128(pc_diff);
        w->Write<uint8_t>(DW_LNS_ADVANCE_LINE);
        w->WriteSLEB128(line_diff);
        w->Write<uint8_t>(DW_LNS_COPY);
      }

      // Increment the pc and line operands.
      pc += pc_diff;
      line += line_diff;
    }
    // Advance the pc to the end of the routine, since the end sequence opcode
    // requires this.
    w->Write<uint8_t>(DW_LNS_ADVANCE_PC);
    w->WriteSLEB128(desc_->CodeSize() - pc);
    WriteExtendedOpcode(w, DW_LNE_END_SEQUENCE, 0);
    total_length.set(static_cast<uint32_t>(w->position() - start));
    return true;
  }

 private:
  void WriteExtendedOpcode(Writer* w, DWARF2ExtendedOpcode op,
                           size_t operands_size) {
    w->Write<uint8_t>(0);
    w->WriteULEB128(operands_size + 1);
    w->Write<uint8_t>(op);
  }

  static bool ComparePCInfo(const LineInfo::PCInfo& a,
                            const LineInfo::PCInfo& b) {
    if (a.pc_ == b.pc_) {
      if (a.is_statement_ != b.is_statement_) {
        return !b.is_statement_;
      }
      return false;
    }
    return a.pc_ < b.pc_;
  }

  CodeDescription* desc_;
};

#if V8_TARGET_ARCH_X64

class UnwindInfoSection : public DebugSection {
 public:
  explicit UnwindInfoSection(CodeDescription* desc);
  bool WriteBodyInternal(Writer* w) override;

  int WriteCIE(Writer* w);
  void WriteFDE(Writer* w, int);

  void WriteFDEStateOnEntry(Writer* w);
  void WriteFDEStateAfterRBPPush(Writer* w);
  void WriteFDEStateAfterRBPSet(Writer* w);
  void WriteFDEStateAfterRBPPop(Writer* w);

  void WriteLength(Writer* w, Writer::Slot<uint32_t>* length_slot,
                   int initial_position);

 private:
  CodeDescription* desc_;

  // DWARF3 Specification, Table 7.23
  enum CFIInstructions {
    DW_CFA_ADVANCE_LOC = 0x40,
    DW_CFA_OFFSET = 0x80,
    DW_CFA_RESTORE = 0xC0,
    DW_CFA_NOP = 0x00,
    DW_CFA_SET_LOC = 0x01,
    DW_CFA_ADVANCE_LOC1 = 0x02,
    DW_CFA_ADVANCE_LOC2 = 0x03,
    DW_CFA_ADVANCE_LOC4 = 0x04,
    DW_CFA_OFFSET_EXTENDED = 0x05,
    DW_CFA_RESTORE_EXTENDED = 0x06,
    DW_CFA_UNDEFINED = 0x07,
    DW_CFA_SAME_VALUE = 0x08,
    DW_CFA_REGISTER = 0x09,
    DW_CFA_REMEMBER_STATE = 0x0A,
    DW_CFA_RESTORE_STATE = 0x0B,
    DW_CFA_DEF_CFA = 0x0C,
    DW_CFA_DEF_CFA_REGISTER = 0x0D,
    DW_CFA_DEF_CFA_OFFSET = 0x0E,

    DW_CFA_DEF_CFA_EXPRESSION = 0x0F,
    DW_CFA_EXPRESSION = 0x10,
    DW_CFA_OFFSET_EXTENDED_SF = 0x11,
    DW_CFA_DEF_CFA_SF = 0x12,
    DW_CFA_DEF_CFA_OFFSET_SF = 0x13,
    DW_CFA_VAL_OFFSET = 0x14,
    DW_CFA_VAL_OFFSET_SF = 0x15,
    DW_CFA_VAL_EXPRESSION = 0x16
  };

  // System V ABI, AMD64 Supplement, Version 0.99.5, Figure 3.36
  enum RegisterMapping {
    // Only the relevant ones have been added to reduce clutter.
    AMD64_RBP = 6,
    AMD64_RSP = 7,
    AMD64_RA = 16
  };

  enum CFIConstants {
    CIE_ID = 0,
    CIE_VERSION = 1,
    CODE_ALIGN_FACTOR = 1,
    DATA_ALIGN_FACTOR = 1,
    RETURN_ADDRESS_REGISTER = AMD64_RA
  };
};

void UnwindInfoSection::WriteLength(Writer* w,
                                    Writer::Slot<uint32_t>* length_slot,
                                    int initial_position) {
  uint32_t align = (w->position() - initial_position) % kSystemPointerSize;

  if (align != 0) {
    for (uint32_t i = 0; i < (kSystemPointerSize - align); i++) {
      w->Write<uint8_t>(DW_CFA_NOP);
    }
  }

  DCHECK_EQ((w->position() - initial_position) % kSystemPointerSize, 0);
  length_slot->set(static_cast<uint32_t>(w->position() - initial_position));
}

UnwindInfoSection::UnwindInfoSection(CodeDescription* desc)
#ifdef __ELF
    : ELFSection(".eh_frame", TYPE_X86_64_UNWIND, 1),
#else
    : MachOSection("__eh_frame", "__TEXT", sizeof(uintptr_t),
                   MachOSection::S_REGULAR),
#endif
      desc_(desc) {
}

int UnwindInfoSection::WriteCIE(Writer* w) {
  Writer::Slot<uint32_t> cie_length_slot = w->CreateSlotHere<uint32_t>();
  uint32_t cie_position = static_cast<uint32_t>(w->position());

  // Write out the CIE header. Currently no 'common instructions' are
  // emitted onto the CIE; every FDE has its own set of instructions.

  w->Write<uint32_t>(CIE_ID);
  w->Write<uint8_t>(CIE_VERSION);
  w->Write<uint8_t>(0);  // Null augmentation string.
  w->WriteSLEB128(CODE_ALIGN_FACTOR);
  w->WriteSLEB128(DATA_ALIGN_FACTOR);
  w->Write<uint8_t>(RETURN_ADDRESS_REGISTER);

  WriteLength(w, &cie_length_slot, cie_position);

  return cie_position;
}

void UnwindInfoSection::WriteFDE(Writer* w, int cie_position) {
  // The only FDE for this function. The CFA is the current RBP.
  Writer::Slot<uint32_t> fde_length_slot = w->CreateSlotHere<uint32_t>();
  int fde_position = static_cast<uint32_t>(w->position());
  w->Write<int32_t>(fde_position - cie_position + 4);

  w->Write<uintptr_t>(desc_->CodeStart());
  w->Write<uintptr_t>(desc_->CodeSize());

  WriteFDEStateOnEntry(w);
  WriteFDEStateAfterRBPPush(w);
  WriteFDEStateAfterRBPSet(w);
  WriteFDEStateAfterRBPPop(w);

  WriteLength(w, &fde_length_slot, fde_position);
}

void UnwindInfoSection::WriteFDEStateOnEntry(Writer* w) {
  // The first state, just after the control has been transferred to the the
  // function.

  // RBP for this function will be the value of RSP after pushing the RBP
  // for the previous function. The previous RBP has not been pushed yet.
  w->Write<uint8_t>(DW_CFA_DEF_CFA_SF);
  w->WriteULEB128(AMD64_RSP);
  w->WriteSLEB128(-kSystemPointerSize);

  // The RA is stored at location CFA + kCallerPCOffset. This is an invariant,
  // and hence omitted from the next states.
  w->Write<uint8_t>(DW_CFA_OFFSET_EXTENDED);
  w->WriteULEB128(AMD64_RA);
  w->WriteSLEB128(StandardFrameConstants::kCallerPCOffset);

  // The RBP of the previous function is still in RBP.
  w->Write<uint8_t>(DW_CFA_SAME_VALUE);
  w->WriteULEB128(AMD64_RBP);

  // Last location described by this entry.
  w->Write<uint8_t>(DW_CFA_SET_LOC);
  w->Write<uint64_t>(
      desc_->GetStackStateStartAddress(CodeDescription::POST_RBP_PUSH));
}

void UnwindInfoSection::WriteFDEStateAfterRBPPush(Writer* w) {
  // The second state, just after RBP has been pushed.

  // RBP / CFA for this function is now the current RSP, so just set the
  // offset from the previous rule (from -8) to 0.
  w->Write<uint8_t>(DW_CFA_DEF_CFA_OFFSET);
  w->WriteULEB128(0);

  // The previous RBP is stored at CFA + kCallerFPOffset. This is an invariant
  // in this and the next state, and hence omitted in the next state.
  w->Write<uint8_t>(DW_CFA_OFFSET_EXTENDED);
  w->WriteULEB128(AMD64_RBP);
  w->WriteSLEB128(StandardFrameConstants::kCallerFPOffset);

  // Last location described by this entry.
  w->Write<uint8_t>(DW_CFA_SET_LOC);
  w->Write<uint64_t>(
      desc_->GetStackStateStartAddress(CodeDescription::POST_RBP_SET));
}

void UnwindInfoSection::WriteFDEStateAfterRBPSet(Writer* w) {
  // The third state, after the RBP has been set.

  // The CFA can now directly be set to RBP.
  w->Write<uint8_t>(DW_CFA_DEF_CFA);
  w->WriteULEB128(AMD64_RBP);
  w->WriteULEB128(0);

  // Last location described by this entry.
  w->Write<uint8_t>(DW_CFA_SET_LOC);
  w->Write<uint64_t>(
      desc_->GetStackStateStartAddress(CodeDescription::POST_RBP_POP));
}

void UnwindInfoSection::WriteFDEStateAfterRBPPop(Writer* w) {
  // The fourth (final) state. The RBP has been popped (just before issuing a
  // return).

  // The CFA can is now calculated in the same way as in the first state.
  w->Write<uint8_t>(DW_CFA_DEF_CFA_SF);
  w->WriteULEB128(AMD64_RSP);
  w->WriteSLEB128(-kSystemPointerSize);

  // The RBP
  w->Write<uint8_t>(DW_CFA_OFFSET_EXTENDED);
  w->WriteULEB128(AMD64_RBP);
  w->WriteSLEB128(StandardFrameConstants::kCallerFPOffset);

  // Last location described by this entry.
  w->Write<uint8_t>(DW_CFA_SET_LOC);
  w->Write<uint64_t>(desc_->CodeEnd());
}

bool UnwindInfoSection::WriteBodyInternal(Writer* w) {
  uint32_t cie_position = WriteCIE(w);
  WriteFDE(w, cie_position);
  return true;
}

#endif  // V8_TARGET_ARCH_X64

static void CreateDWARFSections(CodeDescription* desc, Zone* zone,
                                DebugObject* obj) {
  if (desc->IsLineInfoAvailable()) {
    obj->AddSection(zone->New<DebugInfoSection>(desc));
    obj->AddSection(zone->New<DebugAbbrevSection>(desc));
    obj->AddSection(zone->New<DebugLineSection>(desc));
  }
#if V8_TARGET_ARCH_X64
  obj->AddSection(zone->New<UnwindInfoSection>(desc));
#endif
}

// -------------------------------------------------------------------
// Binary GDB JIT Interface as described in
//   http://sourceware.org/gdb/onlinedocs/gdb/Declarations.html
extern "C" {
enum JITAction { JIT_NOACTION = 0, JIT_REGISTER_FN, JIT_UNREGISTER_FN };

struct JITCodeEntry {
  JITCodeEntry* next_;
  JITCodeEntry* prev_;
  Address symfile_addr_;
  uint64_t symfile_size_;
};

struct JITDescriptor {
  uint32_t version_;
  uint32_t action_flag_;
  JITCodeEntry* relevant_entry_;
  JITCodeEntry* first_entry_;
};

// GDB will place breakpoint into this function.
// To prevent GCC from inlining or removing it we place noinline attribute
// and inline assembler statement inside.
void __attribute__((noinline)) __jit_debug_register_code() { __asm__(""); }

// GDB will inspect contents of this descriptor.
// Static initialization is necessary to prevent GDB from seeing
// uninitialized descriptor.
JITDescriptor __jit_debug_descriptor = {1, 0, nullptr, nullptr};

#ifdef OBJECT_PRINT
void __gdb_print_v8_object(TaggedBase object) {
  StdoutStream os;
  Print(object, os);
  os << std::flush;
}
#endif
}

static JITCodeEntry* CreateCodeEntry(Address symfile_addr,
                                     uintptr_t symfile_size) {
  JITCodeEntry* entry = static_cast<JITCodeEntry*>(
      base::Malloc(sizeof(JITCodeEntry) + symfile_size));

  entry->symfile_addr_ = reinterpret_cast<Address>(entry + 1);
  entry->symfile_size_ = symfile_size;
  MemCopy(reinterpret_cast<void*>(entry->symfile_addr_),
          reinterpret_cast<void*>(symfile_addr), symfile_size);

  entry->prev_ = entry->next_ = nullptr;

  return entry;
}

static void DestroyCodeEntry(JITCodeEntry* entry) { base::Free(entry); }

static void RegisterCodeEntry(JITCodeEntry* entry) {
  entry->next_ = __jit_debug_descriptor.first_entry_;
  if (entry->next_ != nullptr) entry->next_->prev_ = entry;
  __jit_debug_descriptor.first_entry_ = __jit_debug_descriptor.relevant_entry_ =
      entry;

  __jit_debug_descriptor.action_flag_ = JIT_REGISTER_FN;
  __jit_debug_register_code();
}

static void UnregisterCodeEntry(JITCodeEntry* entry) {
  if (entry->prev_ != nullptr) {
    entry->prev_->next_ = entry->next_;
  } else {
    __jit_debug_descriptor.first_entry_ = entry->next_;
  }

  if (entry->next_ != nullptr) {
    entry->next_->prev_ = entry->prev_;
  }

  __jit_debug_descriptor.relevant_entry_ = entry;
  __jit_debug_descriptor.action_flag_ = JIT_UNREGISTER_FN;
  __jit_debug_register_code();
}

static JITCodeEntry* CreateELFObject(CodeDescription* desc, Isolate* isolate) {
#ifdef __MACH_O
  Zone zone(isolate->allocator(), ZONE_NAME);
  MachO mach_o(&zone);
  Writer w(&mach_o);

  const uint32_t code_alignment = static_cast<uint32_t>(kCodeAlignment);
  static_assert(code_alignment == kCodeAlignment,
                "Unsupported code alignment value");
  mach_o.AddSection(zone.New<MachOTextSection>(
      code_alignment, desc->CodeStart(), desc->CodeSize()));

  CreateDWARFSections(desc, &zone, &mach_o);

  mach_o.Write(&w, desc->CodeStart(), desc->CodeSize());
#else
  Zone zone(isolate->allocator(), ZONE_NAME);
  ELF elf(&zone);
  Writer w(&elf);

  size_t text_section_index = elf.AddSection(zone.New<FullHeaderELFSection>(
      ".text", ELFSection::TYPE_NOBITS, kCodeAlignment, desc->CodeStart(), 0,
      desc->CodeSize(), ELFSection::FLAG_ALLOC | ELFSection::FLAG_EXEC));

  CreateSymbolsTable(desc, &zone, &elf, text_section_index);

  CreateDWARFSections(desc, &zone, &elf);

  elf.Write(&w);
#endif

  return CreateCodeEntry(reinterpret_cast<Address>(w.buffer()), w.position());
}

// Like base::AddressRegion::StartAddressLess but also compares |end| when
// |begin| is equal.
struct AddressRegionLess {
  bool operator()(const base::AddressRegion& a,
                  const base::AddressRegion& b) const {
    if (a.begin() == b.begin()) return a.end() < b.end();
    return a.begin() < b.begin();
  }
};

using CodeMap = std::map<base::AddressRegion, JITCodeEntry*, AddressRegionLess>;

static CodeMap* GetCodeMap() {
  // TODO(jgruber): Don't leak.
  static CodeMap* code_map = nullptr;
  if (code_map == nullptr) code_map = new CodeMap();
  return code_map;
}

static uint32_t HashCodeAddress(Address addr) {
  static const uintptr_t kGoldenRatio = 2654435761u;
  return static_cast<uint32_t>((addr >> kCodeAlignmentBits) * kGoldenRatio);
}

static base::HashMap* GetLineMap() {
  static base::HashMap* line_map = nullptr;
  if (line_map == nullptr) {
    line_map = new base::HashMap();
  }
  return line_map;
}

static void PutLineInfo(Address addr, LineInfo* info) {
  base::HashMap* line_map = GetLineMap();
  base::HashMap::Entry* e = line_map->LookupOrInsert(
      reinterpret_cast<void*>(addr), HashCodeAddress(addr));
  if (e->value != nullptr) delete static_cast<LineInfo*>(e->value);
  e->value = info;
}

static LineInfo* GetLineInfo(Address addr) {
  void* value = GetLineMap()->Remove(reinterpret_cast<void*>(addr),
                                     HashCodeAddress(addr));
  return static_cast<LineInfo*>(value);
}

static void AddUnwindInfo(CodeDescription* desc) {
#if V8_TARGET_ARCH_X64
  if (desc->is_function()) {
    // To avoid propagating unwinding information through
    // compilation pipeline we use an approximation.
    // For most use cases this should not affect usability.
    static const int kFramePointerPushOffset = 1;
    static const int kFramePointerSetOffset = 4;
    static const int kFramePointerPopOffset = -3;

    uintptr_t frame_pointer_push_address =
        desc->CodeStart() + kFramePointerPushOffset;

    uintptr_t frame_pointer_set_address =
        desc->CodeStart() + kFramePointerSetOffset;

    uintptr_t frame_pointer_pop_address =
        desc->CodeEnd() + kFramePointerPopOffset;

    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_PUSH,
                                    frame_pointer_push_address);
    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_SET,
                                    frame_pointer_set_address);
    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_POP,
                                    frame_pointer_pop_address);
  } else {
    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_PUSH,
                                    desc->CodeStart());
    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_SET,
                                    desc->CodeStart());
    desc->SetStackStateStartAddress(CodeDescription::POST_RBP_POP,
                                    desc->CodeEnd());
  }
#endif  // V8_TARGET_ARCH_X64
}

static base::LazyMutex mutex = LAZY_MUTEX_INITIALIZER;

static std::optional<std::pair<CodeMap::iterator, CodeMap::iterator>>
GetOverlappingRegions(CodeMap* map, const base::AddressRegion region) {
  DCHECK_LT(region.begin(), region.end());

  if (map->empty()) return {};

  // Find the first overlapping entry.

  // If successful, points to the first element not less than `region`. The
  // returned iterator has the key in `first` and the value in `second`.
  auto it = map->lower_bound(region);
  auto start_it = it;

  if (it == map->end()) {
    start_it = map->begin();
    // Find the first overlapping entry.
    for (; start_it != map->end(); ++start_it) {
      if (start_it->first.end() > region.begin()) {
        break;
      }
    }
  } else if (it != map->begin()) {
    for (--it; it != map->begin(); --it) {
      if ((*it).first.end() <= region.begin()) break;
      start_it = it;
    }
    if (it == map->begin() && it->first.end() > region.begin()) {
      start_it = it;
    }
  }

  if (start_it == map->end()) {
    return {};
  }

  // Find the first non-overlapping entry after `region`.

  const auto end_it = map->lower_bound({region.end(), 0});

  // Return a range containing intersecting regions.

  if (std::distance(start_it, end_it) < 1)
    return {};  // No overlapping entries.

  return {{start_it, end_it}};
}

// Remove entries from the map that intersect the given address region,
// and deregister them from GDB.
static void RemoveJITCodeEntries(CodeMap* map,
                                 const base::AddressRegion region) {
  if (auto overlap = GetOverlappingRegions(map, region)) {
    auto start_it = overlap->first;
    auto end_it = overlap->second;
    for (auto it = start_it; it != end_it; it++) {
      JITCodeEntry* old_entry = (*it).second;
      UnregisterCodeEntry(old_entry);
      DestroyCodeEntry(old_entry);
    }

    map->erase(start_it, end_it);
  }
}

// Insert the entry into the map and register it with GDB.
static void AddJITCodeEntry(CodeMap* map, const base::AddressRegion region,
                            JITCodeEntry* entry, bool dump_if_enabled,
                            const char* name_hint) {
#if defined(DEBUG) && !V8_OS_WIN
  static int file_num = 0;
  if (v8_flags.gdbjit_dump && dump_if_enabled) {
    static const int kMaxFileNameSize = 64;
    char file_name[64];

    SNPrintF(base::Vector<char>(file_name, kMaxFileNameSize),
             "/tmp/elfdump%s%d.o", (name_hint != nullptr) ? name_hint : "",
             file_num++);
    WriteBytes(file_name, reinterpret_cast<uint8_t*>(entry->symfile_addr_),
               static_cast<int>(entry->symfile_size_));
  }
#endif

  auto result = map->emplace(region, entry);
  DCHECK(result.second);  // Insertion happened.
  USE(result);

  RegisterCodeEntry(entry);
}

static void AddCode(const char* name, base::AddressRegion region,
                    Tagged<SharedFunctionInfo> shared, LineInfo* lineinfo,
                    Isolate* isolate, bool is_function) {
  DisallowGarbageCollection no_gc;
  CodeDescription code_desc(name, region, shared, lineinfo, is_function);

  CodeMap* code_map = GetCodeMap();
  RemoveJITCodeEntries(code_map, region);

  if (!v8_flags.gdbjit_full && !code_desc.IsLineInfoAvailable()) {
    delete lineinfo;
    return;
  }

  AddUnwindInfo(&code_desc);
  JITCodeEntry* entry = CreateELFObject(&code_desc, isolate);

  delete lineinfo;

  const char* name_hint = nullptr;
  bool should_dump = false;
  if (v8_flags.gdbjit_dump) {
    if (strlen(v8_flags.gdbjit_dump_filter) == 0) {
      name_hint = name;
      should_dump = true;
    } else if (name != nullptr) {
      name_hint = strstr(name, v8_flags.gdbjit_dump_filter);
      should_dump = (name_hint != nullptr);
    }
  }
  AddJITCodeEntry(code_map, region, entry, should_dump, name_hint);
}

void EventHandler(const v8::JitCodeEvent* event) {
  if (!v8_flags.gdbjit) return;
  if ((event->code_type != v8::JitCodeEvent::JIT_CODE) &&
      (event->code_type != v8::JitCodeEvent::WASM_CODE)) {
    return;
  }
  base::MutexGuard lock_guard(mutex.Pointer());
  switch (event->type) {
    case v8::JitCodeEvent::CODE_ADDED: {
      Address addr = reinterpret_cast<Address>(event->code_start);
      LineInfo* lineinfo = GetLineInfo(addr);
      std::string event_name(event->name.str, event->name.len);
      // It's called UnboundScript in the API but it's a SharedFunctionInfo.
      Tagged<SharedFunctionInfo> shared =
          event->script.IsEmpty() ? Tagged<SharedFunctionInfo>()
                                  : *Utils::OpenDirectHandle(*event->script);
      Isolate* isolate = reinterpret_cast<Isolate*>(event->isolate);
      bool is_function = false;
      // TODO(zhin): See if we can use event->code_type to determine
      // is_function, the difference currently is that JIT_CODE is SparkPlug,
      // TurboProp, TurboFan, whereas CodeKindIsOptimizedJSFunction is only
      // TurboProp and TurboFan. is_function is used for AddUnwindInfo, and the
      // prologue that SP generates probably matches that of TP/TF, so we can
      // use event->code_type here instead of finding the Code.
      // TODO(zhin): Rename is_function to be more accurate.
      if (event->code_type == v8::JitCodeEvent::JIT_CODE) {
        Tagged<Code> lookup_result =
            isolate->heap()->FindCodeForInnerPointer(addr);
        is_function = CodeKindIsOptimizedJSFunction(lookup_result->kind());
      }
      AddCode(event_name.c_str(), {addr, event->code_len}, shared, lineinfo,
              isolate, is_function);
      break;
    }
    case v8::JitCodeEvent::CODE_MOVED:
      // Enabling the GDB JIT interface should disable code compaction.
      UNREACHABLE();
    case v8::JitCodeEvent::CODE_REMOVED:
      // Do nothing.  Instead, adding code causes eviction of any entry whose
      // address range intersects the address range of the added code.
      break;
    case v8::JitCodeEvent::CODE_ADD_LINE_POS_INFO: {
      LineInfo* line_info = reinterpret_cast<LineInfo*>(event->user_data);
      line_info->SetPosition(static_cast<intptr_t>(event->line_info.offset),
                             static_cast<int>(event->line_info.pos),
                             event->line_info.position_type ==
                                 v8::JitCodeEvent::STATEMENT_POSITION);
      break;
    }
    case v8::JitCodeEvent::CODE_START_LINE_INFO_RECORDING: {
      v8::JitCodeEvent* mutable_event = const_cast<v8::JitCodeEvent*>(event);
      mutable_event->user_data = new LineInfo();
      break;
    }
    case v8::JitCodeEvent::CODE_END_LINE_INFO_RECORDING: {
      LineInfo* line_info = reinterpret_cast<LineInfo*>(event->user_data);
      PutLineInfo(reinterpret_cast<Address>(event->code_start), line_info);
      break;
    }
  }
}

void AddRegionForTesting(const base::AddressRegion region) {
  // For testing purposes we don't care about JITCodeEntry, pass nullptr.
  auto result = GetCodeMap()->emplace(region, nullptr);
  DCHECK(result.second);  // Insertion happened.
  USE(result);
}

void ClearCodeMapForTesting() { GetCodeMap()->clear(); }

size_t NumOverlapEntriesForTesting(const base::AddressRegion region) {
  if (auto overlaps = GetOverlappingRegions(GetCodeMap(), region)) {
    return std::distance(overlaps->first, overlaps->second);
  }
  return 0;
}

#endif
}  // namespace GDBJITInterface
}  // namespace internal
}  // namespace v8

#undef __MACH_O
#undef __ELF
