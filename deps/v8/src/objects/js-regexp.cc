// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-regexp.h"

#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/objects/code.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/regexp.h"

namespace v8 {
namespace internal {

Handle<JSRegExpResultIndices> JSRegExpResultIndices::BuildIndices(
    Isolate* isolate, Handle<RegExpMatchInfo> match_info,
    Handle<Object> maybe_names) {
  Handle<JSRegExpResultIndices> indices(Handle<JSRegExpResultIndices>::cast(
      isolate->factory()->NewJSObjectFromMap(
          isolate->regexp_result_indices_map())));

  // Initialize indices length to avoid having a partially initialized object
  // should GC be triggered by creating a NewFixedArray.
  indices->set_length(Smi::zero());

  // Build indices array from RegExpMatchInfo.
  int num_indices = match_info->number_of_capture_registers();
  int num_results = num_indices >> 1;
  Handle<FixedArray> indices_array =
      isolate->factory()->NewFixedArray(num_results);
  JSArray::SetContent(indices, indices_array);

  for (int i = 0; i < num_results; i++) {
    const int start_offset =
        match_info->capture(RegExpMatchInfo::capture_start_index(i));
    const int end_offset =
        match_info->capture(RegExpMatchInfo::capture_end_index(i));

    // Any unmatched captures are set to undefined, otherwise we set them to a
    // subarray of the indices.
    if (start_offset == -1) {
      indices_array->set(i, ReadOnlyRoots(isolate).undefined_value());
    } else {
      Handle<FixedArray> indices_sub_array(
          isolate->factory()->NewFixedArray(2));
      indices_sub_array->set(0, Smi::FromInt(start_offset));
      indices_sub_array->set(1, Smi::FromInt(end_offset));
      Handle<JSArray> indices_sub_jsarray =
          isolate->factory()->NewJSArrayWithElements(indices_sub_array,
                                                     PACKED_SMI_ELEMENTS, 2);
      indices_array->set(i, *indices_sub_jsarray);
    }
  }

  // If there are no capture groups, set the groups property to undefined.
  FieldIndex groups_index = FieldIndex::ForDescriptor(
      indices->map(), InternalIndex(kGroupsDescriptorIndex));
  if (IsUndefined(*maybe_names, isolate)) {
    indices->FastPropertyAtPut(groups_index,
                               ReadOnlyRoots(isolate).undefined_value());
    return indices;
  }

  // Create a groups property which returns a dictionary of named captures to
  // their corresponding capture indices.
  Handle<FixedArray> names(Handle<FixedArray>::cast(maybe_names));
  int num_names = names->length() >> 1;
  Handle<HeapObject> group_names;
  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    group_names = isolate->factory()->NewSwissNameDictionary(num_names);
  } else {
    group_names = isolate->factory()->NewNameDictionary(num_names);
  }
  Handle<PropertyDictionary> group_names_dict =
      Handle<PropertyDictionary>::cast(group_names);
  for (int i = 0; i < num_names; i++) {
    int base_offset = i * 2;
    int name_offset = base_offset;
    int index_offset = base_offset + 1;
    Handle<String> name(String::cast(names->get(name_offset)), isolate);
    Tagged<Smi> smi_index = Smi::cast(names->get(index_offset));
    Handle<Object> capture_indices(indices_array->get(smi_index.value()),
                                   isolate);
    if (!IsUndefined(*capture_indices, isolate)) {
      capture_indices = Handle<JSArray>::cast(capture_indices);
    }
    InternalIndex group_entry = group_names_dict->FindEntry(isolate, name);
    // Duplicate group entries are possible if the capture groups are in
    // different alternatives, i.e. only one of them can actually match.
    // Therefore when we find a duplicate entry, either the current entry is
    // undefined (didn't match anything) or the indices for the current capture
    // are undefined. In the latter case we don't do anything, in the former
    // case we update the entry.
    if (group_entry.is_found()) {
      DCHECK(v8_flags.js_regexp_duplicate_named_groups);
      if (!IsUndefined(*capture_indices, isolate)) {
        DCHECK(IsUndefined(group_names_dict->ValueAt(group_entry), isolate));
        group_names_dict->ValueAtPut(group_entry, *capture_indices);
      }
    } else {
      group_names_dict =
          PropertyDictionary::Add(isolate, group_names_dict, name,
                                  capture_indices, PropertyDetails::Empty());
    }
  }

  // Convert group_names to a JSObject and store at the groups property of the
  // result indices.
  Handle<FixedArrayBase> elements = isolate->factory()->empty_fixed_array();
  Handle<HeapObject> null =
      Handle<HeapObject>::cast(isolate->factory()->null_value());
  Handle<JSObject> js_group_names =
      isolate->factory()->NewSlowJSObjectWithPropertiesAndElements(
          null, group_names, elements);
  indices->FastPropertyAtPut(groups_index, *js_group_names);
  return indices;
}

uint32_t JSRegExp::backtrack_limit() const {
  CHECK_EQ(type_tag(), IRREGEXP);
  return static_cast<uint32_t>(Smi::ToInt(DataAt(kIrregexpBacktrackLimit)));
}

// static
base::Optional<JSRegExp::Flags> JSRegExp::FlagsFromString(
    Isolate* isolate, Handle<String> flags) {
  const int length = flags->length();

  // A longer flags string cannot be valid.
  if (length > JSRegExp::kFlagCount) return {};

  RegExpFlags value;
  FlatStringReader reader(isolate, String::Flatten(isolate, flags));

  for (int i = 0; i < length; i++) {
    base::Optional<RegExpFlag> flag = JSRegExp::FlagFromChar(reader.Get(i));
    if (!flag.has_value()) return {};
    if (value & flag.value()) return {};  // Duplicate.
    value |= flag.value();
  }

  return JSRegExp::AsJSRegExpFlags(value);
}

// static
Handle<String> JSRegExp::StringFromFlags(Isolate* isolate,
                                         JSRegExp::Flags flags) {
  FlagsBuffer buffer;
  return isolate->factory()->NewStringFromAsciiChecked(
      FlagsToString(flags, &buffer));
}

// static
MaybeHandle<JSRegExp> JSRegExp::New(Isolate* isolate, Handle<String> pattern,
                                    Flags flags, uint32_t backtrack_limit) {
  Handle<JSFunction> constructor = isolate->regexp_function();
  Handle<JSRegExp> regexp =
      Handle<JSRegExp>::cast(isolate->factory()->NewJSObject(constructor));

  return JSRegExp::Initialize(regexp, pattern, flags, backtrack_limit);
}

Tagged<Object> JSRegExp::code(IsolateForSandbox isolate, bool is_latin1) const {
  DCHECK_EQ(type_tag(), JSRegExp::IRREGEXP);
  Tagged<Object> value = DataAt(code_index(is_latin1));
  DCHECK(IsSmi(value) || IsCodeWrapper(value));
  // TODO(saelo): it would be nice if we could directly use a code pointer to
  // reference our Code rather than use the CodeWrapper object. However, this
  // is currently not possible since we use essentially a FixedArray to store
  // all our fields, and a code pointer isn't a tagged pointer. Instead, we
  // should consider adding a trusted pointer field that references either the
  // bytecode or the native code in a sandbox-compatible way.
  if (IsCodeWrapper(value)) {
    value = CodeWrapper::cast(value)->code(isolate);
  }
  DCHECK(IsSmi(value) || IsCode(value));
  return value;
}

void JSRegExp::set_code(bool is_latin1, Handle<Code> code) {
  SetDataAt(code_index(is_latin1), code->wrapper());
}

Tagged<Object> JSRegExp::bytecode(bool is_latin1) const {
  DCHECK(type_tag() == JSRegExp::IRREGEXP ||
         type_tag() == JSRegExp::EXPERIMENTAL);
  return DataAt(bytecode_index(is_latin1));
}

void JSRegExp::set_bytecode_and_trampoline(Isolate* isolate,
                                           Handle<ByteArray> bytecode) {
  SetDataAt(kIrregexpLatin1BytecodeIndex, *bytecode);
  SetDataAt(kIrregexpUC16BytecodeIndex, *bytecode);

  Handle<Code> trampoline = BUILTIN_CODE(isolate, RegExpExperimentalTrampoline);
  SetDataAt(JSRegExp::kIrregexpLatin1CodeIndex, trampoline->wrapper());
  SetDataAt(JSRegExp::kIrregexpUC16CodeIndex, trampoline->wrapper());
}

bool JSRegExp::ShouldProduceBytecode() {
  return v8_flags.regexp_interpret_all ||
         (v8_flags.regexp_tier_up && !MarkedForTierUp());
}

// Only irregexps are subject to tier-up.
bool JSRegExp::CanTierUp() {
  return v8_flags.regexp_tier_up && type_tag() == JSRegExp::IRREGEXP;
}

// An irregexp is considered to be marked for tier up if the tier-up ticks
// value reaches zero.
bool JSRegExp::MarkedForTierUp() {
  DCHECK(IsFixedArray(data()));

  if (!CanTierUp()) {
    return false;
  }

  return Smi::ToInt(DataAt(kIrregexpTicksUntilTierUpIndex)) == 0;
}

void JSRegExp::ResetLastTierUpTick() {
  DCHECK(v8_flags.regexp_tier_up);
  DCHECK_EQ(type_tag(), JSRegExp::IRREGEXP);
  int tier_up_ticks = Smi::ToInt(DataAt(kIrregexpTicksUntilTierUpIndex)) + 1;
  FixedArray::cast(data())->set(JSRegExp::kIrregexpTicksUntilTierUpIndex,
                                Smi::FromInt(tier_up_ticks));
}

void JSRegExp::TierUpTick() {
  DCHECK(v8_flags.regexp_tier_up);
  DCHECK_EQ(type_tag(), JSRegExp::IRREGEXP);
  int tier_up_ticks = Smi::ToInt(DataAt(kIrregexpTicksUntilTierUpIndex));
  if (tier_up_ticks == 0) {
    return;
  }
  FixedArray::cast(data())->set(JSRegExp::kIrregexpTicksUntilTierUpIndex,
                                Smi::FromInt(tier_up_ticks - 1));
}

void JSRegExp::MarkTierUpForNextExec() {
  DCHECK(v8_flags.regexp_tier_up);
  DCHECK_EQ(type_tag(), JSRegExp::IRREGEXP);
  FixedArray::cast(data())->set(JSRegExp::kIrregexpTicksUntilTierUpIndex,
                                Smi::zero());
}

// static
MaybeHandle<JSRegExp> JSRegExp::Initialize(Handle<JSRegExp> regexp,
                                           Handle<String> source,
                                           Handle<String> flags_string) {
  Isolate* isolate = regexp->GetIsolate();
  base::Optional<Flags> flags =
      JSRegExp::FlagsFromString(isolate, flags_string);
  if (!flags.has_value() ||
      !RegExp::VerifyFlags(JSRegExp::AsRegExpFlags(flags.value()))) {
    THROW_NEW_ERROR(
        isolate,
        NewSyntaxError(MessageTemplate::kInvalidRegExpFlags, flags_string),
        JSRegExp);
  }
  return Initialize(regexp, source, flags.value());
}

namespace {

bool IsLineTerminator(int c) {
  // Expected to return true for '\n', '\r', 0x2028, and 0x2029.
  return unibrow::IsLineTerminator(static_cast<unibrow::uchar>(c));
}

// TODO(jgruber): Consider merging CountAdditionalEscapeChars and
// WriteEscapedRegExpSource into a single function to deduplicate dispatch logic
// and move related code closer to each other.
template <typename Char>
int CountAdditionalEscapeChars(Handle<String> source, bool* needs_escapes_out) {
  DisallowGarbageCollection no_gc;
  int escapes = 0;
  bool needs_escapes = false;
  bool in_character_class = false;
  base::Vector<const Char> src = source->GetCharVector<Char>(no_gc);
  for (int i = 0; i < src.length(); i++) {
    const Char c = src[i];
    if (c == '\\') {
      if (i + 1 < src.length() && IsLineTerminator(src[i + 1])) {
        // This '\' is ignored since the next character itself will be escaped.
        escapes--;
      } else {
        // Escape. Skip next character, which will be copied verbatim;
        i++;
      }
    } else if (c == '/' && !in_character_class) {
      // Not escaped forward-slash needs escape.
      needs_escapes = true;
      escapes++;
    } else if (c == '[') {
      in_character_class = true;
    } else if (c == ']') {
      in_character_class = false;
    } else if (c == '\n') {
      needs_escapes = true;
      escapes++;
    } else if (c == '\r') {
      needs_escapes = true;
      escapes++;
    } else if (static_cast<int>(c) == 0x2028) {
      needs_escapes = true;
      escapes += std::strlen("\\u2028") - 1;
    } else if (static_cast<int>(c) == 0x2029) {
      needs_escapes = true;
      escapes += std::strlen("\\u2029") - 1;
    } else {
      DCHECK(!IsLineTerminator(c));
    }
  }
  DCHECK(!in_character_class);
  DCHECK_GE(escapes, 0);
  DCHECK_IMPLIES(escapes != 0, needs_escapes);
  *needs_escapes_out = needs_escapes;
  return escapes;
}

template <typename Char>
void WriteStringToCharVector(base::Vector<Char> v, int* d, const char* string) {
  int s = 0;
  while (string[s] != '\0') v[(*d)++] = string[s++];
}

template <typename Char, typename StringType>
Handle<StringType> WriteEscapedRegExpSource(Handle<String> source,
                                            Handle<StringType> result) {
  DisallowGarbageCollection no_gc;
  base::Vector<const Char> src = source->GetCharVector<Char>(no_gc);
  base::Vector<Char> dst(result->GetChars(no_gc), result->length());
  int s = 0;
  int d = 0;
  bool in_character_class = false;
  while (s < src.length()) {
    const Char c = src[s];
    if (c == '\\') {
      if (s + 1 < src.length() && IsLineTerminator(src[s + 1])) {
        // This '\' is ignored since the next character itself will be escaped.
        s++;
        continue;
      } else {
        // Escape. Copy this and next character.
        dst[d++] = src[s++];
      }
      if (s == src.length()) break;
    } else if (c == '/' && !in_character_class) {
      // Not escaped forward-slash needs escape.
      dst[d++] = '\\';
    } else if (c == '[') {
      in_character_class = true;
    } else if (c == ']') {
      in_character_class = false;
    } else if (c == '\n') {
      WriteStringToCharVector(dst, &d, "\\n");
      s++;
      continue;
    } else if (c == '\r') {
      WriteStringToCharVector(dst, &d, "\\r");
      s++;
      continue;
    } else if (static_cast<int>(c) == 0x2028) {
      WriteStringToCharVector(dst, &d, "\\u2028");
      s++;
      continue;
    } else if (static_cast<int>(c) == 0x2029) {
      WriteStringToCharVector(dst, &d, "\\u2029");
      s++;
      continue;
    } else {
      DCHECK(!IsLineTerminator(c));
    }
    dst[d++] = src[s++];
  }
  DCHECK_EQ(result->length(), d);
  DCHECK(!in_character_class);
  return result;
}

MaybeHandle<String> EscapeRegExpSource(Isolate* isolate,
                                       Handle<String> source) {
  DCHECK(source->IsFlat());
  if (source->length() == 0) return isolate->factory()->query_colon_string();
  bool one_byte = String::IsOneByteRepresentationUnderneath(*source);
  bool needs_escapes = false;
  int additional_escape_chars =
      one_byte ? CountAdditionalEscapeChars<uint8_t>(source, &needs_escapes)
               : CountAdditionalEscapeChars<base::uc16>(source, &needs_escapes);
  if (!needs_escapes) return source;
  int length = source->length() + additional_escape_chars;
  if (one_byte) {
    Handle<SeqOneByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               isolate->factory()->NewRawOneByteString(length),
                               String);
    return WriteEscapedRegExpSource<uint8_t>(source, result);
  } else {
    Handle<SeqTwoByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               isolate->factory()->NewRawTwoByteString(length),
                               String);
    return WriteEscapedRegExpSource<base::uc16>(source, result);
  }
}

}  // namespace

// static
MaybeHandle<JSRegExp> JSRegExp::Initialize(Handle<JSRegExp> regexp,
                                           Handle<String> source, Flags flags,
                                           uint32_t backtrack_limit) {
  Isolate* isolate = regexp->GetIsolate();
  Factory* factory = isolate->factory();
  // If source is the empty string we set it to "(?:)" instead as
  // suggested by ECMA-262, 5th, section 15.10.4.1.
  if (source->length() == 0) source = factory->query_colon_string();

  source = String::Flatten(isolate, source);

  RETURN_ON_EXCEPTION(
      isolate,
      RegExp::Compile(isolate, regexp, source, JSRegExp::AsRegExpFlags(flags),
                      backtrack_limit),
      JSRegExp);

  Handle<String> escaped_source;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, escaped_source,
                             EscapeRegExpSource(isolate, source), JSRegExp);

  regexp->set_source(*escaped_source);
  regexp->set_flags(Smi::FromInt(flags));

  Tagged<Map> map = regexp->map();
  Tagged<Object> constructor = map->GetConstructor();
  if (IsJSFunction(constructor) &&
      JSFunction::cast(constructor)->initial_map() == map) {
    // If we still have the original map, set in-object properties directly.
    regexp->InObjectPropertyAtPut(JSRegExp::kLastIndexFieldIndex,
                                  Smi::FromInt(kInitialLastIndexValue),
                                  SKIP_WRITE_BARRIER);
  } else {
    // Map has changed, so use generic, but slower, method.
    RETURN_ON_EXCEPTION(
        isolate,
        Object::SetProperty(
            isolate, regexp, factory->lastIndex_string(),
            Handle<Smi>(Smi::FromInt(kInitialLastIndexValue), isolate)),
        JSRegExp);
  }

  return regexp;
}

}  // namespace internal
}  // namespace v8
