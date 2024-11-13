// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json/json-parser.h"

#include <optional>

#include "src/base/strings.h"
#include "src/builtins/builtins.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/elements-kind.h"
#include "src/objects/field-type.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/map-updater.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/property-details.h"
#include "src/roots/roots.h"
#include "src/strings/char-predicates-inl.h"
#include "src/strings/string-hasher.h"
#include "src/utils/boxed-float.h"

namespace v8 {
namespace internal {

namespace {

constexpr JsonToken GetOneCharJsonToken(uint8_t c) {
  // clang-format off
  return
     c == '"' ? JsonToken::STRING :
     IsDecimalDigit(c) ?  JsonToken::NUMBER :
     c == '-' ? JsonToken::NUMBER :
     c == '[' ? JsonToken::LBRACK :
     c == '{' ? JsonToken::LBRACE :
     c == ']' ? JsonToken::RBRACK :
     c == '}' ? JsonToken::RBRACE :
     c == 't' ? JsonToken::TRUE_LITERAL :
     c == 'f' ? JsonToken::FALSE_LITERAL :
     c == 'n' ? JsonToken::NULL_LITERAL :
     c == ' ' ? JsonToken::WHITESPACE :
     c == '\t' ? JsonToken::WHITESPACE :
     c == '\r' ? JsonToken::WHITESPACE :
     c == '\n' ? JsonToken::WHITESPACE :
     c == ':' ? JsonToken::COLON :
     c == ',' ? JsonToken::COMMA :
     JsonToken::ILLEGAL;
  // clang-format on
}

// Table of one-character tokens, by character (0x00..0xFF only).
static const constexpr JsonToken one_char_json_tokens[256] = {
#define CALL_GET_SCAN_FLAGS(N) GetOneCharJsonToken(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
#define CALL_GET_SCAN_FLAGS(N) GetOneCharJsonToken(128 + N),
        INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

enum class EscapeKind : uint8_t {
  kIllegal,
  kSelf,
  kBackspace,
  kTab,
  kNewLine,
  kFormFeed,
  kCarriageReturn,
  kUnicode
};

using EscapeKindField = base::BitField8<EscapeKind, 0, 3>;
using MayTerminateStringField = EscapeKindField::Next<bool, 1>;
using NumberPartField = MayTerminateStringField::Next<bool, 1>;

constexpr bool MayTerminateJsonString(uint8_t flags) {
  return MayTerminateStringField::decode(flags);
}

constexpr EscapeKind GetEscapeKind(uint8_t flags) {
  return EscapeKindField::decode(flags);
}

constexpr bool IsNumberPart(uint8_t flags) {
  return NumberPartField::decode(flags);
}

constexpr uint8_t GetJsonScanFlags(uint8_t c) {
  // clang-format off
  return (c == 'b' ? EscapeKindField::encode(EscapeKind::kBackspace)
          : c == 't' ? EscapeKindField::encode(EscapeKind::kTab)
          : c == 'n' ? EscapeKindField::encode(EscapeKind::kNewLine)
          : c == 'f' ? EscapeKindField::encode(EscapeKind::kFormFeed)
          : c == 'r' ? EscapeKindField::encode(EscapeKind::kCarriageReturn)
          : c == 'u' ? EscapeKindField::encode(EscapeKind::kUnicode)
          : c == '"' ? EscapeKindField::encode(EscapeKind::kSelf)
          : c == '\\' ? EscapeKindField::encode(EscapeKind::kSelf)
          : c == '/' ? EscapeKindField::encode(EscapeKind::kSelf)
          : EscapeKindField::encode(EscapeKind::kIllegal)) |
         (c < 0x20 ? MayTerminateStringField::encode(true)
          : c == '"' ? MayTerminateStringField::encode(true)
          : c == '\\' ? MayTerminateStringField::encode(true)
          : MayTerminateStringField::encode(false)) |
         NumberPartField::encode(c == '.' ||
                                 c == 'e' ||
                                 c == 'E' ||
                                 IsDecimalDigit(c) ||
                                 c == '-' ||
                                 c == '+');
  // clang-format on
}

// Table of one-character scan flags, by character (0x00..0xFF only).
static const constexpr uint8_t character_json_scan_flags[256] = {
#define CALL_GET_SCAN_FLAGS(N) GetJsonScanFlags(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
#define CALL_GET_SCAN_FLAGS(N) GetJsonScanFlags(128 + N),
        INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

}  // namespace

MaybeHandle<Object> JsonParseInternalizer::Internalize(
    Isolate* isolate, Handle<Object> result, Handle<Object> reviver,
    Handle<String> source, MaybeHandle<Object> val_node) {
  DCHECK(IsCallable(*reviver));
  JsonParseInternalizer internalizer(isolate, Cast<JSReceiver>(reviver),
                                     source);
  Handle<JSObject> holder =
      isolate->factory()->NewJSObject(isolate->object_function());
  Handle<String> name = isolate->factory()->empty_string();
  JSObject::AddProperty(isolate, holder, name, result, NONE);
  return internalizer.InternalizeJsonProperty<kWithSource>(
      holder, name, val_node.ToHandleChecked(), result);
}

template <JsonParseInternalizer::WithOrWithoutSource with_source>
MaybeHandle<Object> JsonParseInternalizer::InternalizeJsonProperty(
    Handle<JSReceiver> holder, Handle<String> name, Handle<Object> val_node,
    Handle<Object> snapshot) {
  DCHECK_EQ(with_source == kWithSource,
            !val_node.is_null() && !snapshot.is_null());
  DCHECK(IsCallable(*reviver_));
  HandleScope outer_scope(isolate_);
  Handle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, value, Object::GetPropertyOrElement(isolate_, holder, name));

  // When with_source == kWithSource, the source text is passed to the reviver
  // if the reviver has not mucked with the originally parsed value.
  //
  // When with_source == kWithoutSource, this is unused.
  bool pass_source_to_reviver =
      with_source == kWithSource && Object::SameValue(*value, *snapshot);

  if (IsJSReceiver(*value)) {
    Handle<JSReceiver> object = Cast<JSReceiver>(value);
    Maybe<bool> is_array = Object::IsArray(object);
    if (is_array.IsNothing()) return MaybeHandle<Object>();
    if (is_array.FromJust()) {
      Handle<Object> length_object;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate_, length_object,
          Object::GetLengthFromArrayLike(isolate_, object));
      double length = Object::NumberValue(*length_object);
      if (pass_source_to_reviver) {
        auto val_nodes_and_snapshots = Cast<FixedArray>(val_node);
        int snapshot_length = val_nodes_and_snapshots->length() / 2;
        for (int i = 0; i < length; i++) {
          HandleScope inner_scope(isolate_);
          DirectHandle<Object> index = isolate_->factory()->NewNumber(i);
          Handle<String> index_name =
              isolate_->factory()->NumberToString(index);
          // Even if the array pointer snapshot matched, it's possible the
          // array had new elements added that are not in the snapshotted
          // elements.
          const bool rv =
              i < snapshot_length
                  ? RecurseAndApply<kWithSource>(
                        object, index_name,
                        handle(val_nodes_and_snapshots->get(i * 2), isolate_),
                        handle(val_nodes_and_snapshots->get(i * 2 + 1),
                               isolate_))
                  : RecurseAndApply<kWithoutSource>(
                        object, index_name, Handle<Object>(), Handle<Object>());
          if (!rv) {
            return MaybeHandle<Object>();
          }
        }
      } else {
        for (int i = 0; i < length; i++) {
          HandleScope inner_scope(isolate_);
          DirectHandle<Object> index = isolate_->factory()->NewNumber(i);
          Handle<String> index_name =
              isolate_->factory()->NumberToString(index);
          if (!RecurseAndApply<kWithoutSource>(
                  object, index_name, Handle<Object>(), Handle<Object>())) {
            return MaybeHandle<Object>();
          }
        }
      }
    } else {
      Handle<FixedArray> contents;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate_, contents,
          KeyAccumulator::GetKeys(isolate_, object, KeyCollectionMode::kOwnOnly,
                                  ENUMERABLE_STRINGS,
                                  GetKeysConversion::kConvertToString));
      if (pass_source_to_reviver) {
        auto val_nodes_and_snapshots = Cast<ObjectTwoHashTable>(val_node);
        for (int i = 0; i < contents->length(); i++) {
          HandleScope inner_scope(isolate_);
          Handle<String> key_name(Cast<String>(contents->get(i)), isolate_);
          auto property_val_node_and_snapshot =
              val_nodes_and_snapshots->Lookup(isolate_, key_name);
          Handle<Object> property_val_node(property_val_node_and_snapshot[0],
                                           isolate_);
          Handle<Object> property_snapshot(property_val_node_and_snapshot[1],
                                           isolate_);
          // Even if the object pointer snapshot matched, it's possible the
          // object had new properties added that are not in the snapshotted
          // contents.
          const bool rv =
              !IsTheHole(*property_snapshot)
                  ? RecurseAndApply<kWithSource>(
                        object, key_name, property_val_node, property_snapshot)
                  : RecurseAndApply<kWithoutSource>(
                        object, key_name, Handle<Object>(), Handle<Object>());
          if (!rv) {
            return MaybeHandle<Object>();
          }
        }
      } else {
        for (int i = 0; i < contents->length(); i++) {
          HandleScope inner_scope(isolate_);
          Handle<String> key_name(Cast<String>(contents->get(i)), isolate_);
          if (!RecurseAndApply<kWithoutSource>(
                  object, key_name, Handle<Object>(), Handle<Object>())) {
            return MaybeHandle<Object>();
          }
        }
      }
    }
  }

    Handle<JSObject> context =
        isolate_->factory()->NewJSObject(isolate_->object_function());
    if (pass_source_to_reviver && IsString(*val_node)) {
      JSReceiver::CreateDataProperty(isolate_, context,
                                     isolate_->factory()->source_string(),
                                     val_node, Just(kThrowOnError))
          .Check();
    }
    Handle<Object> argv[] = {name, value, context};
    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate_, result, Execution::Call(isolate_, reviver_, holder, 3, argv));
    return outer_scope.CloseAndEscape(result);
}

template <JsonParseInternalizer::WithOrWithoutSource with_source>
bool JsonParseInternalizer::RecurseAndApply(Handle<JSReceiver> holder,
                                            Handle<String> name,
                                            Handle<Object> val_node,
                                            Handle<Object> snapshot) {
  STACK_CHECK(isolate_, false);
  DCHECK(IsCallable(*reviver_));
  Handle<Object> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate_, result,
      InternalizeJsonProperty<with_source>(holder, name, val_node, snapshot),
      false);
  Maybe<bool> change_result = Nothing<bool>();
  if (IsUndefined(*result, isolate_)) {
    change_result = JSReceiver::DeletePropertyOrElement(holder, name,
                                                        LanguageMode::kSloppy);
  } else {
    PropertyDescriptor desc;
    desc.set_value(Cast<JSAny>(result));
    desc.set_configurable(true);
    desc.set_enumerable(true);
    desc.set_writable(true);
    change_result = JSReceiver::DefineOwnProperty(isolate_, holder, name, &desc,
                                                  Just(kDontThrow));
  }
  MAYBE_RETURN(change_result, false);
  return true;
}

template <typename Char>
JsonParser<Char>::JsonParser(Isolate* isolate, Handle<String> source)
    : isolate_(isolate),
      hash_seed_(HashSeed(isolate)),
      object_constructor_(isolate_->object_function()),
      original_source_(source) {
  size_t start = 0;
  size_t length = source->length();
  PtrComprCageBase cage_base(isolate);
  if (IsSlicedString(*source, cage_base)) {
    Tagged<SlicedString> string = Cast<SlicedString>(*source);
    start = string->offset();
    Tagged<String> parent = string->parent();
    if (IsThinString(parent, cage_base))
      parent = Cast<ThinString>(parent)->actual();
    source_ = handle(parent, isolate);
  } else {
    source_ = String::Flatten(isolate, source);
  }

  if (StringShape(*source_, cage_base).IsExternal()) {
    chars_ =
        static_cast<const Char*>(Cast<SeqExternalString>(*source_)->GetChars());
    chars_may_relocate_ = false;
  } else {
    DisallowGarbageCollection no_gc;
    isolate->main_thread_local_heap()->AddGCEpilogueCallback(
        UpdatePointersCallback, this);
    chars_ = Cast<SeqString>(*source_)->GetChars(no_gc);
    chars_may_relocate_ = true;
  }
  cursor_ = chars_ + start;
  end_ = cursor_ + length;
}

template <typename Char>
bool JsonParser<Char>::IsSpecialString() {
  // The special cases are undefined, NaN, Infinity, and {} being passed to the
  // parse method
  int offset = IsSlicedString(*original_source_)
                   ? Cast<SlicedString>(*original_source_)->offset()
                   : 0;
  size_t length = original_source_->length();
#define CASES(V)       \
  V("[object Object]") \
  V("undefined")       \
  V("Infinity")        \
  V("NaN")
  switch (length) {
#define CASE(n)          \
  case arraysize(n) - 1: \
    return CompareCharsEqual(chars_ + offset, n, arraysize(n) - 1);
    CASES(CASE)
    default:
      return false;
  }
#undef CASE
#undef CASES
}

template <typename Char>
MessageTemplate JsonParser<Char>::GetErrorMessageWithEllipses(
    DirectHandle<Object>& arg, DirectHandle<Object>& arg2, int pos) {
  MessageTemplate message;
  Factory* factory = this->factory();
  arg = factory->LookupSingleCharacterStringFromCode(*cursor_);
  int origin_source_length = original_source_->length();
  // only provide context for strings with at least
  // kMinOriginalSourceLengthForContext charcacters in length
  if (origin_source_length >= kMinOriginalSourceLengthForContext) {
    int substring_start = 0;
    int substring_end = origin_source_length;
    if (pos < kMaxContextCharacters) {
      message =
          MessageTemplate::kJsonParseUnexpectedTokenStartStringWithContext;
      // Output the string followed by elipses
      substring_end = pos + kMaxContextCharacters;
    } else if (pos >= kMaxContextCharacters &&
               pos < origin_source_length - kMaxContextCharacters) {
      message =
          MessageTemplate::kJsonParseUnexpectedTokenSurroundStringWithContext;
      // Add context before and after position of bad token surrounded by
      // elipses
      substring_start = pos - kMaxContextCharacters;
      substring_end = pos + kMaxContextCharacters;
    } else {
      message = MessageTemplate::kJsonParseUnexpectedTokenEndStringWithContext;
      // Add ellipses followed by some context before bad token
      substring_start = pos - kMaxContextCharacters;
    }
    arg2 =
        factory->NewSubString(original_source_, substring_start, substring_end);
  } else {
    arg2 = original_source_;
    // Output the entire string without ellipses but provide the token which
    // was unexpected
    message = MessageTemplate::kJsonParseUnexpectedTokenShortString;
  }
  return message;
}

template <typename Char>
MessageTemplate JsonParser<Char>::LookUpErrorMessageForJsonToken(
    JsonToken token, DirectHandle<Object>& arg, DirectHandle<Object>& arg2,
    int pos) {
  MessageTemplate message;
  switch (token) {
    case JsonToken::EOS:
      message = MessageTemplate::kJsonParseUnexpectedEOS;
      break;
    case JsonToken::NUMBER:
      message = MessageTemplate::kJsonParseUnexpectedTokenNumber;
      break;
    case JsonToken::STRING:
      message = MessageTemplate::kJsonParseUnexpectedTokenString;
      break;
    default:
      // Output entire string without ellipses and don't provide the token
      // that was unexpected because it makes the error messages more confusing
      if (IsSpecialString()) {
        arg = original_source_;
        message = MessageTemplate::kJsonParseShortString;
      } else {
        message = GetErrorMessageWithEllipses(arg, arg2, pos);
      }
  }
  return message;
}

template <typename Char>
void JsonParser<Char>::CalculateFileLocation(DirectHandle<Object>& line,
                                             DirectHandle<Object>& column) {
  // JSON allows only \r and \n as line terminators.
  // (See https://www.json.org/json-en.html - "whitespace")
  int line_number = 1;
  const Char* start =
      chars_ + (IsSlicedString(*original_source_)
                    ? Cast<SlicedString>(*original_source_)->offset()
                    : 0);
  const Char* last_line_break = start;
  const Char* cursor = start;
  const Char* end = cursor_;  // cursor_ points to the position of the error.
  for (; cursor < end; ++cursor) {
    if (*cursor == '\r' && cursor < end - 1 && cursor[1] == '\n') {
      // \r\n counts as a single line terminator, as of
      // https://tc39.es/ecma262/#sec-line-terminators. JSON itself does not
      // have a notion of lines or line terminators.
      ++cursor;
    }
    if (*cursor == '\r' || *cursor == '\n') {
      ++line_number;
      last_line_break = cursor + 1;
    }
  }
  int column_number = 1 + static_cast<int>(cursor - last_line_break);
  line = direct_handle(Smi::FromInt(line_number), isolate());
  column = direct_handle(Smi::FromInt(column_number), isolate());
}

template <typename Char>
void JsonParser<Char>::ReportUnexpectedToken(
    JsonToken token, std::optional<MessageTemplate> errorMessage) {
  // Some exception (for example stack overflow) was already thrown.
  if (isolate_->has_exception()) return;

  // Parse failed. Current character is the unexpected token.
  Factory* factory = this->factory();
  int offset = IsSlicedString(*original_source_)
                   ? Cast<SlicedString>(*original_source_)->offset()
                   : 0;
  int pos = position() - offset;
  DirectHandle<Object> arg(Smi::FromInt(pos), isolate());
  DirectHandle<Object> arg2;
  DirectHandle<Object> arg3;
  CalculateFileLocation(arg2, arg3);

  MessageTemplate message =
      errorMessage ? errorMessage.value()
                   : LookUpErrorMessageForJsonToken(token, arg, arg2, pos);

  Handle<Script> script(factory->NewScript(original_source_));
  DCHECK_IMPLIES(isolate_->NeedsSourcePositions(), script->has_line_ends());
  DebuggableStackFrameIterator it(isolate_);
  if (!it.done() && it.is_javascript()) {
    FrameSummary summary = it.GetTopValidFrame();
    script->set_eval_from_shared(summary.AsJavaScript().function()->shared());
    if (IsScript(*summary.script())) {
      script->set_origin_options(
          Cast<Script>(*summary.script())->origin_options());
    }
  }

  // We should sent compile error event because we compile JSON object in
  // separated source file.
  isolate()->debug()->OnCompileError(script);
  MessageLocation location(script, pos, pos + 1);
  isolate()->ThrowAt(factory->NewSyntaxError(message, arg, arg2, arg3),
                     &location);

  // Move the cursor to the end so we won't be able to proceed parsing.
  cursor_ = end_;
}

template <typename Char>
void JsonParser<Char>::ReportUnexpectedCharacter(base::uc32 c) {
  JsonToken token = JsonToken::ILLEGAL;
  if (c == kEndOfString) {
    token = JsonToken::EOS;
  } else if (c <= unibrow::Latin1::kMaxChar) {
    token = one_char_json_tokens[c];
  }
  return ReportUnexpectedToken(token);
}

template <typename Char>
JsonParser<Char>::~JsonParser() {
  if (StringShape(*source_).IsExternal()) {
    // Check that the string shape hasn't changed. Otherwise our GC hooks are
    // broken.
    Cast<SeqExternalString>(*source_);
  } else {
    // Check that the string shape hasn't changed. Otherwise our GC hooks are
    // broken.
    Cast<SeqString>(*source_);
    isolate()->main_thread_local_heap()->RemoveGCEpilogueCallback(
        UpdatePointersCallback, this);
  }
}

template <typename Char>
MaybeHandle<Object> JsonParser<Char>::ParseJson(DirectHandle<Object> reviver) {
  Handle<Object> result;
  // Only record the val node when reviver is callable.
  bool reviver_is_callable = IsCallable(*reviver);
  bool should_track_json_source = reviver_is_callable;
  if (V8_UNLIKELY(should_track_json_source)) {
    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result, ParseJsonValue<true>());
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result, ParseJsonValueRecursive());
  }

  if (!Check(JsonToken::EOS)) {
    ReportUnexpectedToken(
        peek(), MessageTemplate::kJsonParseUnexpectedNonWhiteSpaceCharacter);
    return MaybeHandle<Object>();
  }
  if (isolate_->has_exception()) {
    return MaybeHandle<Object>();
  }
  return result;
}

MaybeHandle<Object> InternalizeJsonProperty(Handle<JSObject> holder,
                                            Handle<String> key);

namespace {
template <typename Char>
JsonToken GetTokenForCharacter(Char c) {
  return V8_LIKELY(c <= unibrow::Latin1::kMaxChar) ? one_char_json_tokens[c]
                                                   : JsonToken::ILLEGAL;
}
}  // namespace

template <typename Char>
void JsonParser<Char>::SkipWhitespace() {
  JsonToken local_next = JsonToken::EOS;

  cursor_ = std::find_if(cursor_, end_, [&](Char c) {
    JsonToken current = GetTokenForCharacter(c);
    bool result = current != JsonToken::WHITESPACE;
    if (V8_LIKELY(result)) local_next = current;
    return result;
  });

  next_ = local_next;
}

template <typename Char>
base::uc32 JsonParser<Char>::ScanUnicodeCharacter() {
  base::uc32 value = 0;
  for (int i = 0; i < 4; i++) {
    int digit = base::HexValue(NextCharacter());
    if (V8_UNLIKELY(digit < 0)) return kInvalidUnicodeCharacter;
    value = value * 16 + digit;
  }
  return value;
}

// Parse any JSON value.
template <typename Char>
JsonString JsonParser<Char>::ScanJsonPropertyKey(JsonContinuation* cont) {
  {
    DisallowGarbageCollection no_gc;
    const Char* start = cursor_;
    base::uc32 first = CurrentCharacter();
    if (first == '\\' && NextCharacter() == 'u') first = ScanUnicodeCharacter();
    if (IsDecimalDigit(first)) {
      if (first == '0') {
        if (NextCharacter() == '"') {
          advance();
          // Record element information.
          cont->elements++;
          DCHECK_LE(0, cont->max_index);
          return JsonString(0);
        }
      } else {
        uint32_t index = first - '0';
        while (true) {
          cursor_ = std::find_if(cursor_ + 1, end_, [&index](Char c) {
            return !TryAddArrayIndexChar(&index, c);
          });

          if (CurrentCharacter() == '"') {
            advance();
            // Record element information.
            cont->elements++;
            cont->max_index = std::max(cont->max_index, index);
            return JsonString(index);
          }

          if (CurrentCharacter() == '\\' && NextCharacter() == 'u') {
            if (TryAddArrayIndexChar(&index, ScanUnicodeCharacter())) continue;
          }

          break;
        }
      }
    }
    // Reset cursor_ to start if the key is not an index.
    cursor_ = start;
  }
  return ScanJsonString(true);
}

class FoldedMutableHeapNumberAllocation {
 public:
  // TODO(leszeks): If allocation alignment is ever enabled, we'll need to add
  // padding fillers between heap numbers.
  static_assert(!USE_ALLOCATION_ALIGNMENT_BOOL);

  FoldedMutableHeapNumberAllocation(Isolate* isolate, int count) {
    if (count == 0) return;
    int size = count * sizeof(HeapNumber);
    raw_bytes_ = isolate->factory()->NewByteArray(size);
  }

  Handle<ByteArray> raw_bytes() const { return raw_bytes_; }

 private:
  Handle<ByteArray> raw_bytes_ = {};
};

class FoldedMutableHeapNumberAllocator {
 public:
  FoldedMutableHeapNumberAllocator(
      Isolate* isolate, FoldedMutableHeapNumberAllocation* allocation,
      DisallowGarbageCollection& no_gc)
      : isolate_(isolate), roots_(isolate) {
    if (allocation->raw_bytes().is_null()) return;

    raw_bytes_ = allocation->raw_bytes();
    mutable_double_address_ =
        reinterpret_cast<Address>(allocation->raw_bytes()->begin());
  }

  ~FoldedMutableHeapNumberAllocator() {
    // Make all mutable HeapNumbers alive.
    if (mutable_double_address_ == 0) {
      DCHECK(raw_bytes_.is_null());
      return;
    }

    DCHECK_EQ(mutable_double_address_,
              reinterpret_cast<Address>(raw_bytes_->end()));
    // Before setting the length of mutable_double_buffer back to zero, we
    // must ensure that the sweeper is not running or has already swept the
    // object's page. Otherwise the GC can add the contents of
    // mutable_double_buffer to the free list.
    isolate_->heap()->EnsureSweepingCompletedForObject(*raw_bytes_);
    raw_bytes_->set_length(0);
  }

  Tagged<HeapNumber> AllocateNext(ReadOnlyRoots roots, Float64 value) {
    DCHECK_GE(mutable_double_address_,
              reinterpret_cast<Address>(raw_bytes_->begin()));
    Tagged<HeapObject> hn = HeapObject::FromAddress(mutable_double_address_);
    hn->set_map_after_allocation(roots.heap_number_map());
    Cast<HeapNumber>(hn)->set_value_as_bits(value.get_bits());
    mutable_double_address_ +=
        ALIGN_TO_ALLOCATION_ALIGNMENT(sizeof(HeapNumber));
    DCHECK_LE(mutable_double_address_,
              reinterpret_cast<Address>(raw_bytes_->end()));
    return Cast<HeapNumber>(hn);
  }

 private:
  Isolate* isolate_;
  ReadOnlyRoots roots_;
  Handle<ByteArray> raw_bytes_ = {};
  Address mutable_double_address_ = 0;
};

// JSDataObjectBuilder is a helper for efficiently building a data object,
// similar (in semantics and efficiency) to a JS object literal, based on
// key/value pairs.
//
// The JSDataObjectBuilder works by first trying to find the right map for the
// object, and then letting the caller stamp out the object fields linearly.
// There are several fast paths that can be fallen out of; if the builder bails
// out, then it's still possible to stamp out the object partially based on the
// last map found, and then continue with slow object setup afterward.
//
// The maps start from the object literal cache (to try to share maps with
// equivalent object literals in JS code). From there, when adding properties,
// there are several fast paths that the builder follows:
//
//   1. At construction, it can be passed an expected final map for the object
//      (e.g. cached from previous runs, or assumed from surrounding objects).
//      If given, then we first check whether the property matches the
//      entry in the DescriptorArray of the final map; if yes, then we don't
//      need to do any map transitions.
//   2. When given a property key, it looks for whether there is exactly one
//      transition away from the current map ("ExpectedTransition").
//      The expected key is passed as a hint to the current property key
//      getter, for e.g. faster internalised string materialisation.
//   3. Otherwise, it searches for whether there is any transition in the
//      current map that matches the key.
//   4. For all of the above, it checks whether the field represntation of the
//      found map matches the representation of the value. If it doesn't, it
//      migrates the map, potentially deprecating it too.
//   5. If there is no transition, it tries to allocate a new map transition,
//      bailing out if this fails.
class JSDataObjectBuilder {
 public:
  // HeapNumberMode determines whether incoming HeapNumber values will be
  // guaranteed to be uniquely owned by this object, and therefore can be used
  // directly as mutable HeapNumbers for double representation fields.
  enum HeapNumberMode {
    kNormalHeapNumbers,
    kHeapNumbersGuaranteedUniquelyOwned
  };
  JSDataObjectBuilder(Isolate* isolate, ElementsKind elements_kind,
                      int expected_named_properties,
                      Handle<Map> expected_final_map,
                      HeapNumberMode heap_number_mode)
      : isolate_(isolate),
        elements_kind_(elements_kind),
        expected_property_count_(expected_named_properties),
        heap_number_mode_(heap_number_mode),
        expected_final_map_(expected_final_map) {
    if (!TryInitializeMapFromExpectedFinalMap()) {
      InitializeMapFromZero();
    }
  }

  // Builds and returns an object whose properties are based on a property
  // iterator.
  //
  // Expects an iterator of the form:
  //
  // struct Iterator {
  //   void Advance();
  //   bool Done();
  //
  //   // Get the key of the current property, optionally returning the hinted
  //   // expected key if applicable.
  //   Handle<String> GetKey(Handle<String> expected_key_hint);
  //
  //   // Get the value of the current property. `will_revisit_value` is true
  //   // if this value will need to be revisited later via RevisitValues().
  //   Handle<Object> GetValue(bool will_revisit_value);
  //
  //   // Return an iterator over the values that were already visited by
  //   // GetValue. Might require caching those values if necessary.
  //   ValueIterator RevisitValues();
  // }
  template <typename PropertyIterator>
  Handle<JSObject> BuildFromIterator(
      PropertyIterator&& it, MaybeHandle<FixedArrayBase> maybe_elements = {}) {
    Handle<String> failed_property_add_key;
    for (; !it.Done(); it.Advance()) {
      Handle<String> property_key;
      if (!TryAddFastPropertyForValue(
              it.GetKeyChars(),
              [&](Handle<String> expected_key) {
                return property_key = it.GetKey(expected_key);
              },
              [&]() { return it.GetValue(true); })) {
        failed_property_add_key = property_key;
        break;
      }
    }

    Handle<FixedArrayBase> elements;
    if (!maybe_elements.ToHandle(&elements)) {
      elements = isolate_->factory()->empty_fixed_array();
    }
    CreateAndInitialiseObject(it.RevisitValues(), elements);

    // Slow path: define remaining named properties.
    for (; !it.Done(); it.Advance()) {
      Handle<String> key;
      if (!failed_property_add_key.is_null()) {
        key = std::exchange(failed_property_add_key, {});
      } else {
        key = it.GetKey({});
      }
#ifdef DEBUG
      uint32_t index;
      DCHECK(!key->AsArrayIndex(&index));
#endif
      Handle<Object> value = it.GetValue(false);
      AddSlowProperty(key, value);
    }

    return object();
  }

  template <typename Char, typename GetKeyFunction, typename GetValueFunction>
  V8_INLINE bool TryAddFastPropertyForValue(base::Vector<const Char> key_chars,
                                            GetKeyFunction&& get_key,
                                            GetValueFunction&& get_value) {
    // The fast path is only valid as long as we haven't allocated an object
    // yet.
    DCHECK(object_.is_null());

    Handle<String> key;
    bool existing_map_found =
        TryFastTransitionToPropertyKey(key_chars, get_key, &key);
    // Unconditionally get the value after getting the transition result.
    DirectHandle<Object> value = get_value();
    if (existing_map_found) {
      // We found a map with a field for our value -- now make sure that field
      // is compatible with our value.
      if (!TryGeneralizeFieldToValue(value)) {
        // TODO(leszeks): Try to stay on the fast path if we just deprecate
        // here.
        return false;
      }
      AdvanceToNextProperty();
      return true;
    }

    // Try to stay on a semi-fast path (being able to stamp out the object
    // fields after creating the correct map) by manually creating the next
    // map here.

    Tagged<DescriptorArray> descriptors = map_->instance_descriptors(isolate_);
    InternalIndex descriptor_number =
        descriptors->SearchWithCache(isolate_, *key, *map_);
    if (descriptor_number.is_found()) {
      // Duplicate property, we need to bail out of even the semi-fast path
      // because we can no longer stamp out values linearly.
      return false;
    }

    if (!TransitionsAccessor::CanHaveMoreTransitions(isolate_, map_)) {
      return false;
    }

    Representation representation =
        Object::OptimalRepresentation(*value, isolate_);
    Handle<FieldType> type =
        Object::OptimalType(*value, isolate_, representation);
    MaybeHandle<Map> maybe_map = Map::CopyWithField(
        isolate_, map_, key, type, NONE, PropertyConstness::kConst,
        representation, INSERT_TRANSITION);
    Handle<Map> next_map;
    if (!maybe_map.ToHandle(&next_map)) return false;
    if (next_map->is_dictionary_map()) return false;

    map_ = next_map;
    if (representation.IsDouble()) {
      RegisterFieldNeedsFreshHeapNumber(value);
    }
    AdvanceToNextProperty();
    return true;
  }

  template <typename ValueIterator>
  V8_INLINE void CreateAndInitialiseObject(
      ValueIterator value_it, DirectHandle<FixedArrayBase> elements) {
    // We've created a map for the first `i` property stack values (which might
    // be all of them). We need to write these properties to a newly allocated
    // object.
    DCHECK(object_.is_null());

    if (current_property_index_ < property_count_in_expected_final_map_) {
      // If we were on the expected map fast path all the way, but never reached
      // the expected final map itself, then finalize the map by rewinding to
      // the one whose property is the actual current property index.
      //
      // TODO(leszeks): Do we actually want to use the final map fast path when
      // we know that the current map _can't_ reach the final map? Will we even
      // hit this case given that we check for matching instance size?
      RewindExpectedFinalMapFastPathToBeforeCurrent();
    }

    if (map_->is_dictionary_map()) {
      // It's only safe to emit a dictionary map when we've not set up any
      // properties, as the caller assumes it can set up the first N properties
      // as fast data properties.
      DCHECK_EQ(current_property_index_, 0);

      Handle<JSObject> object = isolate_->factory()->NewSlowJSObjectFromMap(
          map_, expected_property_count_);
      object->set_elements(*elements);
      object_ = object;
      return;
    }

    // The map should have as many own descriptors as the number of properties
    // we've created so far...
    DCHECK_EQ(current_property_index_, map_->NumberOfOwnDescriptors());

    // ... and all of those properties should be in-object data properties.
    DCHECK_EQ(current_property_index_,
              map_->GetInObjectProperties() - map_->UnusedInObjectProperties());

    // Create a folded mutable HeapNumber allocation area before allocating the
    // object -- this ensures that there is no allocation between the object
    // allocation and its initial fields being initialised, where the verifier
    // would see invalid double field state.
    FoldedMutableHeapNumberAllocation hn_allocation(isolate_,
                                                    extra_heap_numbers_needed_);

    // Allocate the object then immediately start a no_gc scope -- again, this
    // is so the verifier doesn't see invalid double field state.
    Handle<JSObject> object = isolate_->factory()->NewJSObjectFromMap(map_);
    DisallowGarbageCollection no_gc;
    Tagged<JSObject> raw_object = *object;

    raw_object->set_elements(*elements);
    Tagged<DescriptorArray> descriptors =
        raw_object->map()->instance_descriptors();

    WriteBarrierMode mode = raw_object->GetWriteBarrierMode(no_gc);
    FoldedMutableHeapNumberAllocator hn_allocator(isolate_, &hn_allocation,
                                                  no_gc);

    ReadOnlyRoots roots(isolate_);

    // Initialize the in-object properties up to the last added property.
    int current_property_offset = raw_object->GetInObjectPropertyOffset(0);
    for (int i = 0; i < current_property_index_; ++i, ++value_it) {
      InternalIndex descriptor_index(i);
      Tagged<Object> value = **value_it;

      // See comment in RegisterFieldNeedsFreshHeapNumber, we need to allocate
      // HeapNumbers for double representation fields when we can't make
      // existing HeapNumbers mutable, or when we only have a Smi value.
      if (heap_number_mode_ != kHeapNumbersGuaranteedUniquelyOwned ||
          IsSmi(value)) {
        PropertyDetails details = descriptors->GetDetails(descriptor_index);
        if (details.representation().IsDouble()) {
          value = hn_allocator.AllocateNext(
              roots, Float64(static_cast<double>(Cast<Smi>(value).value())));
        }
      }

      DCHECK(FieldIndex::ForPropertyIndex(object->map(), i).is_inobject());
      DCHECK_EQ(current_property_offset,
                FieldIndex::ForPropertyIndex(object->map(), i).offset());
      DCHECK_EQ(current_property_offset,
                object->map()->GetInObjectPropertyOffset(i));
      FieldIndex index = FieldIndex::ForInObjectOffset(current_property_offset,
                                                       FieldIndex::kTagged);
      raw_object->RawFastInobjectPropertyAtPut(index, value, mode);
      current_property_offset += kTaggedSize;
    }
    DCHECK_EQ(current_property_offset, object->map()->GetInObjectPropertyOffset(
                                           current_property_index_));

    object_ = object;
  }

  void AddSlowProperty(Handle<String> key, Handle<Object> value) {
    DCHECK(!object_.is_null());

    LookupIterator it(isolate_, object_, key, object_, LookupIterator::OWN);
    JSObject::DefineOwnPropertyIgnoreAttributes(&it, value, NONE).Check();
  }

  Handle<JSObject> object() {
    DCHECK(!object_.is_null());
    return object_;
  }

 private:
  template <typename Char, typename GetKeyFunction>
  V8_INLINE bool TryFastTransitionToPropertyKey(
      base::Vector<const Char> key_chars, GetKeyFunction&& get_key,
      Handle<String>* key_out) {
    Handle<String> expected_key;
    Handle<Map> target_map;

    InternalIndex descriptor_index(current_property_index_);
    if (IsOnExpectedFinalMapFastPath()) {
      expected_key = handle(
          Cast<String>(
              expected_final_map_->instance_descriptors(isolate_)->GetKey(
                  descriptor_index)),
          isolate_);
      target_map = expected_final_map_;
    } else {
      TransitionsAccessor transitions(isolate_, *map_);
      auto expected_transition = transitions.ExpectedTransition(key_chars);
      if (!expected_transition.first.is_null()) {
        // Directly read out the target while reading out the key, otherwise it
        // might die if `get_key` can allocate.
        target_map = expected_transition.second;

        // We were successful and we are done.
        DCHECK_EQ(target_map->instance_descriptors()
                      ->GetDetails(descriptor_index)
                      .location(),
                  PropertyLocation::kField);
        map_ = target_map;
        return true;
      }
    }

    Handle<String> key = *key_out = get_key(expected_key);
    if (key.is_identical_to(expected_key)) {
      // We were successful and we are done.
      DCHECK_EQ(target_map->instance_descriptors()
                    ->GetDetails(descriptor_index)
                    .location(),
                PropertyLocation::kField);
      map_ = target_map;
      return true;
    }

    if (IsOnExpectedFinalMapFastPath()) {
      // We were on the expected map fast path, but this missed that fast
      // path, so rewind the optimistic setting of the current map and disable
      // this fast path.
      RewindExpectedFinalMapFastPathToBeforeCurrent();
      property_count_in_expected_final_map_ = 0;
    }

    MaybeHandle<Map> maybe_target =
        TransitionsAccessor(isolate_, *map_).FindTransitionToField(key);
    if (!maybe_target.ToHandle(&target_map)) return false;

    map_ = target_map;
    return true;
  }

  V8_INLINE bool TryGeneralizeFieldToValue(DirectHandle<Object> value) {
    DCHECK_LT(current_property_index_, map_->NumberOfOwnDescriptors());

    InternalIndex descriptor_index(current_property_index_);
    PropertyDetails current_details =
        map_->instance_descriptors(isolate_)->GetDetails(descriptor_index);
    Representation expected_representation = current_details.representation();

    DCHECK_EQ(current_details.kind(), PropertyKind::kData);
    DCHECK_EQ(current_details.location(), PropertyLocation::kField);

    if (!Object::FitsRepresentation(*value, expected_representation)) {
      Representation representation =
          Object::OptimalRepresentation(*value, isolate_);
      representation = representation.generalize(expected_representation);
      if (!expected_representation.CanBeInPlaceChangedTo(representation)) {
        // Reconfigure the map for the value, deprecating if necessary. This
        // will only happen for double representation fields.
        if (IsOnExpectedFinalMapFastPath()) {
          // If we're on the fast path, we will have advanced the current map
          // all the way to the final expected map. Make sure to rewind to the
          // "real" current map if this happened.
          //
          // An alternative would be to deprecate the expected final map,
          // migrate it to the new representation, and stay on the fast path.
          // However, this would mean allocating all-new maps (with the new
          // representation) all the way between the current map and the new
          // expected final map; if we later fall off the fast path anyway, then
          // all those newly allocated maps will end up unused.
          RewindExpectedFinalMapFastPathToIncludeCurrent();
          property_count_in_expected_final_map_ = 0;
        }
        MapUpdater mu(isolate_, map_);
        Handle<Map> new_map = mu.ReconfigureToDataField(
            descriptor_index, current_details.attributes(),
            current_details.constness(), representation,
            FieldType::Any(isolate_));

        // We only want to stay on the fast path if we got a fast map.
        if (new_map->is_dictionary_map()) return false;
        map_ = new_map;
        DCHECK(representation.IsDouble());
        RegisterFieldNeedsFreshHeapNumber(value);
      } else {
        // Do the in-place reconfiguration.
        DCHECK(!representation.IsDouble());
        Handle<FieldType> value_type =
            Object::OptimalType(*value, isolate_, representation);
        MapUpdater::GeneralizeField(isolate_, map_, descriptor_index,
                                    current_details.constness(), representation,
                                    value_type);
      }
    } else if (expected_representation.IsHeapObject() &&
               !FieldType::NowContains(
                   map_->instance_descriptors(isolate_)->GetFieldType(
                       descriptor_index),
                   value)) {
      Handle<FieldType> value_type =
          Object::OptimalType(*value, isolate_, expected_representation);
      MapUpdater::GeneralizeField(isolate_, map_, descriptor_index,
                                  current_details.constness(),
                                  expected_representation, value_type);
    } else if (expected_representation.IsDouble()) {
      RegisterFieldNeedsFreshHeapNumber(value);
    }

    DCHECK(FieldType::NowContains(
        map_->instance_descriptors(isolate_)->GetFieldType(descriptor_index),
        value));
    return true;
  }

  bool TryInitializeMapFromExpectedFinalMap() {
    if (expected_final_map_.is_null()) return false;
    if (expected_final_map_->elements_kind() != elements_kind_) return false;

    int property_count_in_expected_final_map =
        expected_final_map_->NumberOfOwnDescriptors();
    if (property_count_in_expected_final_map < expected_property_count_)
      return false;

    map_ = expected_final_map_;
    property_count_in_expected_final_map_ =
        property_count_in_expected_final_map;
    return true;
  }

  void InitializeMapFromZero() {
    // Must be called before any properties are registered.
    DCHECK_EQ(current_property_index_, 0);

    map_ = isolate_->factory()->ObjectLiteralMapFromCache(
        isolate_->native_context(), expected_property_count_);
    if (elements_kind_ == DICTIONARY_ELEMENTS) {
      map_ = Map::AsElementsKind(isolate_, map_, elements_kind_);
    } else {
      DCHECK_EQ(map_->elements_kind(), elements_kind_);
    }
  }

  V8_INLINE bool IsOnExpectedFinalMapFastPath() const {
    DCHECK_IMPLIES(property_count_in_expected_final_map_ > 0,
                   !expected_final_map_.is_null());
    return current_property_index_ < property_count_in_expected_final_map_;
  }

  void RewindExpectedFinalMapFastPathToBeforeCurrent() {
    DCHECK_GT(property_count_in_expected_final_map_, 0);
    if (current_property_index_ == 0) {
      InitializeMapFromZero();
      DCHECK_EQ(0, map_->NumberOfOwnDescriptors());
    }
    if (current_property_index_ == 0) {
      return;
    }
    DCHECK_EQ(*map_, *expected_final_map_);
    map_ = handle(map_->FindFieldOwner(
                      isolate_, InternalIndex(current_property_index_ - 1)),
                  isolate_);
  }

  void RewindExpectedFinalMapFastPathToIncludeCurrent() {
    DCHECK_EQ(*map_, *expected_final_map_);
    map_ = handle(expected_final_map_->FindFieldOwner(
                      isolate_, InternalIndex(current_property_index_)),
                  isolate_);
  }

  V8_INLINE void RegisterFieldNeedsFreshHeapNumber(DirectHandle<Object> value) {
    // We need to allocate a new HeapNumber for double representation fields if
    // the HeapNumber values is not guaranteed to be uniquely owned by this
    // object (and therefore can't be made mutable), or if the value is a Smi
    // and there is no HeapNumber box for this value yet at all.
    if (heap_number_mode_ == kHeapNumbersGuaranteedUniquelyOwned &&
        !IsSmi(*value)) {
      DCHECK(IsHeapNumber(*value));
      return;
    }
    extra_heap_numbers_needed_++;
  }

  V8_INLINE void AdvanceToNextProperty() { current_property_index_++; }

  Isolate* isolate_;
  ElementsKind elements_kind_;
  int expected_property_count_;
  HeapNumberMode heap_number_mode_;

  Handle<Map> map_;
  int current_property_index_ = 0;
  int extra_heap_numbers_needed_ = 0;

  Handle<JSObject> object_;

  Handle<Map> expected_final_map_ = {};
  int property_count_in_expected_final_map_ = 0;
};

class NamedPropertyValueIterator {
 public:
  NamedPropertyValueIterator(const JsonProperty* it, const JsonProperty* end)
      : it_(it), end_(end) {
    DCHECK_LE(it_, end_);
    DCHECK_IMPLIES(it_ != end_, !it_->string.is_index());
  }

  NamedPropertyValueIterator& operator++() {
    DCHECK_LT(it_, end_);
    do {
      it_++;
    } while (it_ != end_ && it_->string.is_index());
    return *this;
  }

  Handle<Object> operator*() { return it_->value; }

  bool operator!=(const NamedPropertyValueIterator& other) const {
    return it_ != other.it_;
  }

 private:
  // We need to store both the current iterator and the iterator end, since we
  // don't want to iterate past the end on operator++ if the last property is an
  // index property.
  const JsonProperty* it_;
  const JsonProperty* end_;
};

template <typename Char>
class JsonParser<Char>::NamedPropertyIterator {
 public:
  NamedPropertyIterator(JsonParser<Char>& parser, const JsonProperty* it,
                        const JsonProperty* end)
      : parser_(parser), it_(it), end_(end) {
    DCHECK_LE(it_, end_);
    while (it_ != end_ && it_->string.is_index()) {
      it_++;
    }
    start_ = it_;
  }

  void Advance() {
    DCHECK_LT(it_, end_);
    do {
      it_++;
    } while (it_ != end_ && it_->string.is_index());
  }

  bool Done() const {
    DCHECK_LE(it_, end_);
    return it_ == end_;
  }

  base::Vector<const Char> GetKeyChars() {
    return parser_.GetKeyChars(it_->string);
  }
  Handle<String> GetKey(Handle<String> expected_key_hint) {
    return parser_.MakeString(it_->string, expected_key_hint);
  }
  Handle<Object> GetValue(bool will_revisit_value) {
    // Revisiting values is free, so we don't need to cache the value anywhere.
    return it_->value;
  }
  NamedPropertyValueIterator RevisitValues() {
    return NamedPropertyValueIterator(start_, it_);
  }

 private:
  JsonParser<Char>& parser_;

  const JsonProperty* start_;
  const JsonProperty* it_;
  const JsonProperty* end_;
};

template <typename Char>
Handle<JSObject> JsonParser<Char>::BuildJsonObject(const JsonContinuation& cont,
                                                   Handle<Map> feedback) {
  if (!feedback.is_null() && feedback->is_deprecated()) {
    feedback = Map::Update(isolate_, feedback);
  }
  size_t start = cont.index;
  DCHECK_LE(start, property_stack_.size());
  int length = static_cast<int>(property_stack_.size() - start);
  int named_length = length - cont.elements;
  DCHECK_LE(0, named_length);

  Handle<FixedArrayBase> elements;
  ElementsKind elements_kind = HOLEY_ELEMENTS;

  // First store the elements.
  if (cont.elements > 0) {
    // Store as dictionary elements if that would use less memory.
    if (ShouldConvertToSlowElements(cont.elements, cont.max_index + 1)) {
      Handle<NumberDictionary> elms =
          NumberDictionary::New(isolate_, cont.elements);
      for (int i = 0; i < length; i++) {
        const JsonProperty& property = property_stack_[start + i];
        if (!property.string.is_index()) continue;
        uint32_t index = property.string.index();
        Handle<Object> value = property.value;
        NumberDictionary::UncheckedSet(isolate_, elms, index, value);
      }
      elms->SetInitialNumberOfElements(length);
      elms->UpdateMaxNumberKey(cont.max_index, Handle<JSObject>::null());
      elements_kind = DICTIONARY_ELEMENTS;
      elements = elms;
    } else {
      Handle<FixedArray> elms =
          factory()->NewFixedArrayWithHoles(cont.max_index + 1);
      DisallowGarbageCollection no_gc;
      Tagged<FixedArray> raw_elements = *elms;
      WriteBarrierMode mode = raw_elements->GetWriteBarrierMode(no_gc);

      for (int i = 0; i < length; i++) {
        const JsonProperty& property = property_stack_[start + i];
        if (!property.string.is_index()) continue;
        uint32_t index = property.string.index();
        DirectHandle<Object> value = property.value;
        raw_elements->set(static_cast<int>(index), *value, mode);
      }
      elements = elms;
    }
  } else {
    elements = factory()->empty_fixed_array();
  }

  JSDataObjectBuilder js_data_object_builder(
      isolate_, elements_kind, named_length, feedback,
      JSDataObjectBuilder::kHeapNumbersGuaranteedUniquelyOwned);

  NamedPropertyIterator it(*this, property_stack_.begin() + start,
                           property_stack_.end());

  return js_data_object_builder.BuildFromIterator(it, elements);
}

template <typename Char>
Handle<Object> JsonParser<Char>::BuildJsonArray(size_t start) {
  int length = static_cast<int>(element_stack_.size() - start);

  ElementsKind kind = PACKED_SMI_ELEMENTS;
  for (size_t i = start; i < element_stack_.size(); i++) {
    Tagged<Object> value = *element_stack_[i];
    if (IsHeapObject(value)) {
      if (IsHeapNumber(Cast<HeapObject>(value))) {
        kind = PACKED_DOUBLE_ELEMENTS;
      } else {
        kind = PACKED_ELEMENTS;
        break;
      }
    }
  }

  Handle<JSArray> array = factory()->NewJSArray(kind, length, length);
  if (kind == PACKED_DOUBLE_ELEMENTS) {
    DisallowGarbageCollection no_gc;
    Tagged<FixedDoubleArray> elements =
        Cast<FixedDoubleArray>(array->elements());
    for (int i = 0; i < length; i++) {
      elements->set(i, Object::NumberValue(*element_stack_[start + i]));
    }
  } else {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> elements = Cast<FixedArray>(array->elements());
    WriteBarrierMode mode = kind == PACKED_SMI_ELEMENTS
                                ? SKIP_WRITE_BARRIER
                                : elements->GetWriteBarrierMode(no_gc);
    for (int i = 0; i < length; i++) {
      elements->set(i, *element_stack_[start + i], mode);
    }
  }
  return array;
}

// Parse rawJSON value.
template <typename Char>
bool JsonParser<Char>::ParseRawJson() {
  if (end_ == cursor_) {
    isolate_->Throw(*isolate_->factory()->NewSyntaxError(
        MessageTemplate::kInvalidRawJsonValue));
    return false;
  }
  next_ = GetTokenForCharacter(*cursor_);
  switch (peek()) {
    case JsonToken::STRING:
      Consume(JsonToken::STRING);
      ScanJsonString(false);
      break;

    case JsonToken::NUMBER:
      ParseJsonNumber();
      break;

    case JsonToken::TRUE_LITERAL:
      ScanLiteral("true");
      break;

    case JsonToken::FALSE_LITERAL:
      ScanLiteral("false");
      break;

    case JsonToken::NULL_LITERAL:
      ScanLiteral("null");
      break;

    default:
      ReportUnexpectedCharacter(CurrentCharacter());
      return false;
  }
  if (isolate_->has_exception()) return false;
  if (cursor_ != end_) {
    isolate_->Throw(*isolate_->factory()->NewSyntaxError(
        MessageTemplate::kInvalidRawJsonValue));
    return false;
  }
  return true;
}

template <typename Char>
V8_INLINE MaybeHandle<Object> JsonParser<Char>::ParseJsonValueRecursive(
    Handle<Map> feedback) {
  SkipWhitespace();
  switch (peek()) {
    case JsonToken::NUMBER:
      return ParseJsonNumber();
    case JsonToken::STRING:
      Consume(JsonToken::STRING);
      return MakeString(ScanJsonString(false));

    case JsonToken::TRUE_LITERAL:
      ScanLiteral("true");
      return factory()->true_value();
    case JsonToken::FALSE_LITERAL:
      ScanLiteral("false");
      return factory()->false_value();
    case JsonToken::NULL_LITERAL:
      ScanLiteral("null");
      return factory()->null_value();

    case JsonToken::LBRACE:
      return ParseJsonObject(feedback);
    case JsonToken::LBRACK:
      return ParseJsonArray();

    case JsonToken::COLON:
    case JsonToken::COMMA:
    case JsonToken::ILLEGAL:
    case JsonToken::RBRACE:
    case JsonToken::RBRACK:
    case JsonToken::EOS:
      ReportUnexpectedCharacter(CurrentCharacter());
      // Pop the continuation stack to correctly tear down handle scopes.
      return MaybeHandle<Object>();

    case JsonToken::WHITESPACE:
      UNREACHABLE();
  }
}

template <typename Char>
MaybeHandle<Object> JsonParser<Char>::ParseJsonObject(Handle<Map> feedback) {
  {
    StackLimitCheck check(isolate_);
    if (V8_UNLIKELY(check.HasOverflowed())) {
      return ParseJsonValue<false>();
    }
  }

  Consume(JsonToken::LBRACE);
  if (Check(JsonToken::RBRACE)) {
    return factory()->NewJSObject(object_constructor_);
  }

  JsonContinuation cont(isolate_, JsonContinuation::kObjectProperty,
                        property_stack_.size());
  bool first = true;
  do {
    ExpectNext(
        JsonToken::STRING,
        first ? MessageTemplate::kJsonParseExpectedPropNameOrRBrace
              : MessageTemplate::kJsonParseExpectedDoubleQuotedPropertyName);
    JsonString key = ScanJsonPropertyKey(&cont);
    ExpectNext(JsonToken::COLON,
               MessageTemplate::kJsonParseExpectedColonAfterPropertyName);
    Handle<Object> value;
    if (V8_UNLIKELY(!ParseJsonValueRecursive().ToHandle(&value))) return {};
    property_stack_.emplace_back(key, value);
    first = false;
  } while (Check(JsonToken::COMMA));

  Expect(JsonToken::RBRACE, MessageTemplate::kJsonParseExpectedCommaOrRBrace);
  Handle<Object> result = BuildJsonObject(cont, feedback);
  property_stack_.resize_no_init(cont.index);
  return cont.scope.CloseAndEscape(result);
}

template <typename Char>
MaybeHandle<Object> JsonParser<Char>::ParseJsonArray() {
  {
    StackLimitCheck check(isolate_);
    if (V8_UNLIKELY(check.HasOverflowed())) {
      return ParseJsonValue<false>();
    }
  }

  Consume(JsonToken::LBRACK);
  if (Check(JsonToken::RBRACK)) {
    return factory()->NewJSArray(0, PACKED_SMI_ELEMENTS);
  }

  HandleScope handle_scope(isolate_);
  size_t start = element_stack_.size();
  Handle<Object> value;
  if (V8_UNLIKELY(!ParseJsonValueRecursive().ToHandle(&value))) return {};
  element_stack_.emplace_back(value);
  while (Check(JsonToken::COMMA)) {
    Handle<Map> feedback;
    if (IsJSObject(*value)) {
      Tagged<Map> maybe_feedback = Cast<JSObject>(*value)->map();
      // Don't consume feedback from objects with a map that's detached
      // from the transition tree.
      if (!maybe_feedback->IsDetached(isolate_)) {
        feedback = handle(maybe_feedback, isolate_);
      }
    }
    if (V8_UNLIKELY(!ParseJsonValueRecursive(feedback).ToHandle(&value))) {
      return {};
    }
    element_stack_.emplace_back(value);
  }

  Expect(JsonToken::RBRACK, MessageTemplate::kJsonParseExpectedCommaOrRBrack);
  Handle<Object> result = BuildJsonArray(start);
  element_stack_.resize_no_init(start);
  return handle_scope.CloseAndEscape(result);
}

// Parse any JSON value.
template <typename Char>
template <bool should_track_json_source>
MaybeHandle<Object> JsonParser<Char>::ParseJsonValue() {
  std::vector<JsonContinuation> cont_stack;

  cont_stack.reserve(16);

  JsonContinuation cont(isolate_, JsonContinuation::kReturn, 0);

  Handle<Object> value;

  // When should_track_json_source is true, we use val_node to record current
  // JSON value's parse node.
  //
  // For primitive values, the val_node is the source string of the JSON value.
  //
  // For JSObject values, the val_node is an ObjectHashTable in which the key is
  // the property name and the first value is the property value's parse
  // node. The order in which properties are defined may be different from the
  // order in which properties are enumerated when calling
  // InternalizeJSONProperty for the JSObject value. E.g., the JSON source
  // string is '{"a": 1, "1": 2}', and the properties enumerate order is ["1",
  // "a"]. Moreover, properties may be defined repeatedly in the JSON string.
  // E.g., the JSON string is '{"a": 1, "a": 1}', and the properties enumerate
  // order is ["a"]. So we cannot use the FixedArray to record the properties's
  // parse node by the order in which properties are defined and we use a
  // ObjectHashTable here to record the property name and the property's parse
  // node. We then look up the property's parse node by the property name when
  // calling InternalizeJSONProperty. The second value associated with the key
  // is the property value's snapshot.
  //
  // For JSArray values, the val_node is a FixedArray containing the parse nodes
  // and snapshots of the elements.
  //
  // For information about snapshotting, see below.
  Handle<Object> val_node;
  // Record the start position and end position for the primitive values.
  int start_position;
  int end_position;

  // Workaround for -Wunused-but-set-variable on old gcc versions (version < 8).
  USE(start_position);
  USE(end_position);

  // element_val_node_stack is used to track all the elements's
  // parse nodes. And we use this to construct the JSArray's
  // parse node and value snapshot.
  SmallVector<Handle<Object>> element_val_node_stack;
  // property_val_node_stack is used to track all the property
  // value's parse nodes. And we use this to construct the
  // JSObject's parse node and value snapshot.
  SmallVector<Handle<Object>> property_val_node_stack;
  while (true) {
    // Produce a json value.
    //
    // Iterate until a value is produced. Starting but not immediately finishing
    // objects and arrays will cause the loop to continue until a first member
    // is completed.
    while (true) {
      SkipWhitespace();
      // The switch is immediately followed by 'break' so we can use 'break' to
      // break out of the loop, and 'continue' to continue the loop.

      if constexpr (should_track_json_source) {
        start_position = position();
      }
      switch (peek()) {
        case JsonToken::STRING:
          Consume(JsonToken::STRING);
          value = MakeString(ScanJsonString(false));
          if constexpr (should_track_json_source) {
            end_position = position();
            val_node = isolate_->factory()->NewSubString(
                source_, start_position, end_position);
          }
          break;

        case JsonToken::NUMBER:
          value = ParseJsonNumber();
          if constexpr (should_track_json_source) {
            end_position = position();
            val_node = isolate_->factory()->NewSubString(
                source_, start_position, end_position);
          }
          break;

        case JsonToken::LBRACE: {
          Consume(JsonToken::LBRACE);
          if (Check(JsonToken::RBRACE)) {
            // TODO(verwaest): Directly use the map instead.
            value = factory()->NewJSObject(object_constructor_);
            if constexpr (should_track_json_source) {
              val_node = ObjectTwoHashTable::New(isolate_, 0);
            }
            break;
          }

          // Start parsing an object with properties.
          cont_stack.emplace_back(std::move(cont));
          cont = JsonContinuation(isolate_, JsonContinuation::kObjectProperty,
                                  property_stack_.size());

          // Parse the property key.
          ExpectNext(JsonToken::STRING,
                     MessageTemplate::kJsonParseExpectedPropNameOrRBrace);
          property_stack_.emplace_back(ScanJsonPropertyKey(&cont));
          if constexpr (should_track_json_source) {
            property_val_node_stack.emplace_back(Handle<Object>());
          }

          ExpectNext(JsonToken::COLON,
                     MessageTemplate::kJsonParseExpectedColonAfterPropertyName);

          // Continue to start producing the first property value.
          continue;
        }

        case JsonToken::LBRACK:
          Consume(JsonToken::LBRACK);
          if (Check(JsonToken::RBRACK)) {
            value = factory()->NewJSArray(0, PACKED_SMI_ELEMENTS);
            if constexpr (should_track_json_source) {
              val_node = factory()->NewFixedArray(0);
            }
            break;
          }

          // Start parsing an array with elements.
          cont_stack.emplace_back(std::move(cont));
          cont = JsonContinuation(isolate_, JsonContinuation::kArrayElement,
                                  element_stack_.size());

          // Continue to start producing the first array element.
          continue;

        case JsonToken::TRUE_LITERAL:
          ScanLiteral("true");
          value = factory()->true_value();
          if constexpr (should_track_json_source) {
            val_node = isolate_->factory()->true_string();
          }
          break;

        case JsonToken::FALSE_LITERAL:
          ScanLiteral("false");
          value = factory()->false_value();
          if constexpr (should_track_json_source) {
            val_node = isolate_->factory()->false_string();
          }
          break;

        case JsonToken::NULL_LITERAL:
          ScanLiteral("null");
          value = factory()->null_value();
          if constexpr (should_track_json_source) {
            val_node = isolate_->factory()->null_string();
          }
          break;

        case JsonToken::COLON:
        case JsonToken::COMMA:
        case JsonToken::ILLEGAL:
        case JsonToken::RBRACE:
        case JsonToken::RBRACK:
        case JsonToken::EOS:
          ReportUnexpectedCharacter(CurrentCharacter());
          // Pop the continuation stack to correctly tear down handle scopes.
          while (!cont_stack.empty()) {
            cont = std::move(cont_stack.back());
            cont_stack.pop_back();
          }
          return MaybeHandle<Object>();

        case JsonToken::WHITESPACE:
          UNREACHABLE();
      }
      // Done producing a value, consume it.
      break;
    }

    // Consume a produced json value.
    //
    // Iterate as long as values are produced (arrays or object literals are
    // finished).
    while (true) {
      // The switch is immediately followed by 'break' so we can use 'break' to
      // break out of the loop, and 'continue' to continue the loop.
      switch (cont.type()) {
        case JsonContinuation::kReturn:
          if constexpr (should_track_json_source) {
            DCHECK(!val_node.is_null());
            Tagged<Object> raw_value = *value;
            parsed_val_node_ = cont.scope.CloseAndEscape(val_node);
            return cont.scope.CloseAndEscape(handle(raw_value, isolate_));
          } else {
            return cont.scope.CloseAndEscape(value);
          }

        case JsonContinuation::kObjectProperty: {
          // Store the previous property value into its property info.
          property_stack_.back().value = value;
          if constexpr (should_track_json_source) {
            property_val_node_stack.back() = val_node;
          }

          if (V8_LIKELY(Check(JsonToken::COMMA))) {
            // Parse the property key.
            ExpectNext(
                JsonToken::STRING,
                MessageTemplate::kJsonParseExpectedDoubleQuotedPropertyName);

            property_stack_.emplace_back(ScanJsonPropertyKey(&cont));
            if constexpr (should_track_json_source) {
              property_val_node_stack.emplace_back(Handle<Object>());
            }
            ExpectNext(
                JsonToken::COLON,
                MessageTemplate::kJsonParseExpectedColonAfterPropertyName);

            // Break to start producing the subsequent property value.
            break;
          }

          Handle<Map> feedback;
          if (cont_stack.size() > 0 &&
              cont_stack.back().type() == JsonContinuation::kArrayElement &&
              cont_stack.back().index < element_stack_.size() &&
              IsJSObject(*element_stack_.back())) {
            Tagged<Map> maybe_feedback =
                Cast<JSObject>(*element_stack_.back())->map();
            // Don't consume feedback from objects with a map that's detached
            // from the transition tree.
            if (!maybe_feedback->IsDetached(isolate_)) {
              feedback = handle(maybe_feedback, isolate_);
            }
          }
          value = BuildJsonObject(cont, feedback);
          Expect(JsonToken::RBRACE,
                 MessageTemplate::kJsonParseExpectedCommaOrRBrace);
          // Return the object.
          if constexpr (should_track_json_source) {
            size_t start = cont.index;
            int num_properties =
                static_cast<int>(property_stack_.size() - start);
            Handle<ObjectTwoHashTable> table =
                ObjectTwoHashTable::New(isolate(), num_properties);
            for (int i = 0; i < num_properties; i++) {
              const JsonProperty& property = property_stack_[start + i];
              Handle<Object> property_val_node =
                  property_val_node_stack[start + i];
              Handle<Object> property_snapshot = property.value;
              Handle<String> key;
              if (property.string.is_index()) {
                key = factory()->Uint32ToString(property.string.index());
              } else {
                key = MakeString(property.string);
              }
              table = ObjectTwoHashTable::Put(
                  isolate(), table, key,
                  {property_val_node, property_snapshot});
            }
            property_val_node_stack.resize_no_init(cont.index);
            DisallowGarbageCollection no_gc;
            Tagged<ObjectTwoHashTable> raw_table = *table;
            value = cont.scope.CloseAndEscape(value);
            val_node = cont.scope.CloseAndEscape(handle(raw_table, isolate_));
          } else {
            value = cont.scope.CloseAndEscape(value);
          }
          property_stack_.resize_no_init(cont.index);

          // Pop the continuation.
          cont = std::move(cont_stack.back());
          cont_stack.pop_back();
          // Consume to produced object.
          continue;
        }

        case JsonContinuation::kArrayElement: {
          // Store the previous element on the stack.
          element_stack_.emplace_back(value);
          if constexpr (should_track_json_source) {
            element_val_node_stack.emplace_back(val_node);
          }
          // Break to start producing the subsequent element value.
          if (V8_LIKELY(Check(JsonToken::COMMA))) break;

          value = BuildJsonArray(cont.index);
          Expect(JsonToken::RBRACK,
                 MessageTemplate::kJsonParseExpectedCommaOrRBrack);
          // Return the array.
          if constexpr (should_track_json_source) {
            size_t start = cont.index;
            int num_elements = static_cast<int>(element_stack_.size() - start);
            DirectHandle<FixedArray> val_node_and_snapshot_array =
                factory()->NewFixedArray(num_elements * 2);
            DisallowGarbageCollection no_gc;
            Tagged<FixedArray> raw_val_node_and_snapshot_array =
                *val_node_and_snapshot_array;
            for (int i = 0; i < num_elements; i++) {
              raw_val_node_and_snapshot_array->set(
                  i * 2, *element_val_node_stack[start + i]);
              raw_val_node_and_snapshot_array->set(i * 2 + 1,
                                                   *element_stack_[start + i]);
            }
            element_val_node_stack.resize_no_init(cont.index);
            value = cont.scope.CloseAndEscape(value);
            val_node = cont.scope.CloseAndEscape(
                handle(raw_val_node_and_snapshot_array, isolate_));
          } else {
            value = cont.scope.CloseAndEscape(value);
          }
          element_stack_.resize_no_init(cont.index);
          // Pop the continuation.
          cont = std::move(cont_stack.back());
          cont_stack.pop_back();
          // Consume the produced array.
          continue;
        }
      }

      // Done consuming a value. Produce next value.
      break;
    }
  }
}

template <typename Char>
void JsonParser<Char>::AdvanceToNonDecimal() {
  cursor_ =
      std::find_if(cursor_, end_, [](Char c) { return !IsDecimalDigit(c); });
}

template <typename Char>
Handle<Object> JsonParser<Char>::ParseJsonNumber() {
  double number;
  int sign = 1;

  {
    const Char* start = cursor_;
    DisallowGarbageCollection no_gc;

    base::uc32 c = *cursor_;
    if (c == '-') {
      sign = -1;
      c = NextCharacter();
    }

    if (c == '0') {
      // Prefix zero is only allowed if it's the only digit before
      // a decimal point or exponent.
      c = NextCharacter();
      if (base::IsInRange(c, 0,
                          static_cast<int32_t>(unibrow::Latin1::kMaxChar)) &&
          IsNumberPart(character_json_scan_flags[c])) {
        if (V8_UNLIKELY(IsDecimalDigit(c))) {
          AllowGarbageCollection allow_before_exception;
          ReportUnexpectedToken(JsonToken::NUMBER);
          return handle(Smi::FromInt(0), isolate_);
        }
      } else if (sign > 0) {
        return handle(Smi::FromInt(0), isolate_);
      }
    } else {
      const Char* smi_start = cursor_;
      static_assert(Smi::IsValid(-999999999));
      static_assert(Smi::IsValid(999999999));
      const int kMaxSmiLength = 9;
      int32_t i = 0;
      const Char* stop = cursor_ + kMaxSmiLength;
      if (stop > end_) stop = end_;
      while (cursor_ < stop && IsDecimalDigit(*cursor_)) {
        i = (i * 10) + ((*cursor_) - '0');
        cursor_++;
      }
      if (V8_UNLIKELY(smi_start == cursor_)) {
        AllowGarbageCollection allow_before_exception;
        ReportUnexpectedToken(
            JsonToken::ILLEGAL,
            MessageTemplate::kJsonParseNoNumberAfterMinusSign);
        return handle(Smi::FromInt(0), isolate_);
      }
      c = CurrentCharacter();
      if (!base::IsInRange(c, 0,
                           static_cast<int32_t>(unibrow::Latin1::kMaxChar)) ||
          !IsNumberPart(character_json_scan_flags[c])) {
        // Smi.
        // TODO(verwaest): Cache?
        return handle(Smi::FromInt(i * sign), isolate_);
      }
      AdvanceToNonDecimal();
    }

    if (CurrentCharacter() == '.') {
      c = NextCharacter();
      if (!IsDecimalDigit(c)) {
        AllowGarbageCollection allow_before_exception;
        ReportUnexpectedToken(
            JsonToken::ILLEGAL,
            MessageTemplate::kJsonParseUnterminatedFractionalNumber);
        return handle(Smi::FromInt(0), isolate_);
      }
      AdvanceToNonDecimal();
    }

    if (AsciiAlphaToLower(CurrentCharacter()) == 'e') {
      c = NextCharacter();
      if (c == '-' || c == '+') c = NextCharacter();
      if (!IsDecimalDigit(c)) {
        AllowGarbageCollection allow_before_exception;
        ReportUnexpectedToken(
            JsonToken::ILLEGAL,
            MessageTemplate::kJsonParseExponentPartMissingNumber);
        return handle(Smi::FromInt(0), isolate_);
      }
      AdvanceToNonDecimal();
    }

    base::Vector<const Char> chars(start, cursor_ - start);
    number = StringToDouble(chars,
                            NO_CONVERSION_FLAG,  // Hex, octal or trailing junk.
                            std::numeric_limits<double>::quiet_NaN());

    DCHECK(!std::isnan(number));
  }

  return factory()->NewNumber(number);
}

namespace {

template <typename Char>
bool Matches(base::Vector<const Char> chars, Handle<String> string) {
  DCHECK(!string.is_null());
  return string->IsEqualTo(chars);
}

}  // namespace

template <typename Char>
template <typename SinkSeqString>
Handle<String> JsonParser<Char>::DecodeString(
    const JsonString& string, Handle<SinkSeqString> intermediate,
    Handle<String> hint) {
  using SinkChar = typename SinkSeqString::Char;
  {
    DisallowGarbageCollection no_gc;
    SinkChar* dest = intermediate->GetChars(no_gc);
    if (!string.has_escape()) {
      DCHECK(!string.internalize());
      CopyChars(dest, chars_ + string.start(), string.length());
      return intermediate;
    }
    DecodeString(dest, string.start(), string.length());

    if (!string.internalize()) return intermediate;

    base::Vector<const SinkChar> data(dest, string.length());
    if (!hint.is_null() && Matches(data, hint)) return hint;
  }

  return factory()->InternalizeString(intermediate, 0, string.length());
}

template <typename Char>
Handle<String> JsonParser<Char>::MakeString(const JsonString& string,
                                            Handle<String> hint) {
  if (string.length() == 0) return factory()->empty_string();

  if (string.internalize() && !string.has_escape()) {
    if (!hint.is_null()) {
      base::Vector<const Char> data(chars_ + string.start(), string.length());
      if (Matches(data, hint)) return hint;
    }
    if (chars_may_relocate_) {
      return factory()->InternalizeString(Cast<SeqString>(source_),
                                          string.start(), string.length(),
                                          string.needs_conversion());
    }
    base::Vector<const Char> chars(chars_ + string.start(), string.length());
    return factory()->InternalizeString(chars, string.needs_conversion());
  }

  if (sizeof(Char) == 1 ? V8_LIKELY(!string.needs_conversion())
                        : string.needs_conversion()) {
    Handle<SeqOneByteString> intermediate =
        factory()->NewRawOneByteString(string.length()).ToHandleChecked();
    return DecodeString(string, intermediate, hint);
  }

  Handle<SeqTwoByteString> intermediate =
      factory()->NewRawTwoByteString(string.length()).ToHandleChecked();
  return DecodeString(string, intermediate, hint);
}

template <typename Char>
template <typename SinkChar>
void JsonParser<Char>::DecodeString(SinkChar* sink, int start, int length) {
  SinkChar* sink_start = sink;
  const Char* cursor = chars_ + start;
  while (true) {
    const Char* end = cursor + length - (sink - sink_start);
    cursor = std::find_if(cursor, end, [&sink](Char c) {
      if (c == '\\') return true;
      *sink++ = c;
      return false;
    });

    if (cursor == end) return;

    cursor++;

    switch (GetEscapeKind(character_json_scan_flags[*cursor])) {
      case EscapeKind::kSelf:
        *sink++ = *cursor;
        break;

      case EscapeKind::kBackspace:
        *sink++ = '\x08';
        break;

      case EscapeKind::kTab:
        *sink++ = '\x09';
        break;

      case EscapeKind::kNewLine:
        *sink++ = '\x0A';
        break;

      case EscapeKind::kFormFeed:
        *sink++ = '\x0C';
        break;

      case EscapeKind::kCarriageReturn:
        *sink++ = '\x0D';
        break;

      case EscapeKind::kUnicode: {
        base::uc32 value = 0;
        for (int i = 0; i < 4; i++) {
          value = value * 16 + base::HexValue(*++cursor);
        }
        if (value <=
            static_cast<base::uc32>(unibrow::Utf16::kMaxNonSurrogateCharCode)) {
          *sink++ = value;
        } else {
          *sink++ = unibrow::Utf16::LeadSurrogate(value);
          *sink++ = unibrow::Utf16::TrailSurrogate(value);
        }
        break;
      }

      case EscapeKind::kIllegal:
        UNREACHABLE();
    }
    cursor++;
  }
}

template <typename Char>
JsonString JsonParser<Char>::ScanJsonString(bool needs_internalization) {
  DisallowGarbageCollection no_gc;
  int start = position();
  int offset = start;
  bool has_escape = false;
  base::uc32 bits = 0;

  while (true) {
    cursor_ = std::find_if(cursor_, end_, [&bits](Char c) {
      if (sizeof(Char) == 2 && V8_UNLIKELY(c > unibrow::Latin1::kMaxChar)) {
        bits |= c;
        return false;
      }
      return MayTerminateJsonString(character_json_scan_flags[c]);
    });

    if (V8_UNLIKELY(is_at_end())) {
      AllowGarbageCollection allow_before_exception;
      ReportUnexpectedToken(JsonToken::ILLEGAL,
                            MessageTemplate::kJsonParseUnterminatedString);
      break;
    }

    if (*cursor_ == '"') {
      int end = position();
      advance();
      int length = end - offset;
      bool convert = sizeof(Char) == 1 ? bits > unibrow::Latin1::kMaxChar
                                       : bits <= unibrow::Latin1::kMaxChar;
      constexpr int kMaxInternalizedStringValueLength = 10;
      bool internalize =
          needs_internalization ||
          (sizeof(Char) == 1 && length < kMaxInternalizedStringValueLength);
      return JsonString(start, length, convert, internalize, has_escape);
    }

    if (*cursor_ == '\\') {
      has_escape = true;
      base::uc32 c = NextCharacter();
      if (V8_UNLIKELY(!base::IsInRange(
              c, 0, static_cast<int32_t>(unibrow::Latin1::kMaxChar)))) {
        AllowGarbageCollection allow_before_exception;
        ReportUnexpectedCharacter(c);
        break;
      }

      switch (GetEscapeKind(character_json_scan_flags[c])) {
        case EscapeKind::kSelf:
        case EscapeKind::kBackspace:
        case EscapeKind::kTab:
        case EscapeKind::kNewLine:
        case EscapeKind::kFormFeed:
        case EscapeKind::kCarriageReturn:
          offset += 1;
          break;

        case EscapeKind::kUnicode: {
          base::uc32 value = ScanUnicodeCharacter();
          if (value == kInvalidUnicodeCharacter) {
            AllowGarbageCollection allow_before_exception;
            ReportUnexpectedToken(JsonToken::ILLEGAL,
                                  MessageTemplate::kJsonParseBadUnicodeEscape);
            return JsonString();
          }
          bits |= value;
          // \uXXXX results in either 1 or 2 Utf16 characters, depending on
          // whether the decoded value requires a surrogate pair.
          offset += 5 - (value > static_cast<base::uc32>(
                                     unibrow::Utf16::kMaxNonSurrogateCharCode));
          break;
        }

        case EscapeKind::kIllegal:
          AllowGarbageCollection allow_before_exception;
          ReportUnexpectedToken(JsonToken::ILLEGAL,
                                MessageTemplate::kJsonParseBadEscapedCharacter);
          return JsonString();
      }

      advance();
      continue;
    }

    DCHECK_LT(*cursor_, 0x20);
    AllowGarbageCollection allow_before_exception;
    ReportUnexpectedToken(JsonToken::ILLEGAL,
                          MessageTemplate::kJsonParseBadControlCharacter);
    break;
  }

  return JsonString();
}

// Explicit instantiation.
template class JsonParser<uint8_t>;
template class JsonParser<uint16_t>;

}  // namespace internal
}  // namespace v8
