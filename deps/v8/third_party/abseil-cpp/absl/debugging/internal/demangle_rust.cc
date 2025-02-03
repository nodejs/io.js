// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/debugging/internal/demangle_rust.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/debugging/internal/decode_rust_punycode.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

namespace {

// Same step limit as the C++ demangler in demangle.cc uses.
constexpr int kMaxReturns = 1 << 17;

bool IsDigit(char c) { return '0' <= c && c <= '9'; }
bool IsLower(char c) { return 'a' <= c && c <= 'z'; }
bool IsUpper(char c) { return 'A' <= c && c <= 'Z'; }
bool IsAlpha(char c) { return IsLower(c) || IsUpper(c); }
bool IsIdentifierChar(char c) { return IsAlpha(c) || IsDigit(c) || c == '_'; }
bool IsLowerHexDigit(char c) { return IsDigit(c) || ('a' <= c && c <= 'f'); }

const char* BasicTypeName(char c) {
  switch (c) {
    case 'a': return "i8";
    case 'b': return "bool";
    case 'c': return "char";
    case 'd': return "f64";
    case 'e': return "str";
    case 'f': return "f32";
    case 'h': return "u8";
    case 'i': return "isize";
    case 'j': return "usize";
    case 'l': return "i32";
    case 'm': return "u32";
    case 'n': return "i128";
    case 'o': return "u128";
    case 'p': return "_";
    case 's': return "i16";
    case 't': return "u16";
    case 'u': return "()";
    case 'v': return "...";
    case 'x': return "i64";
    case 'y': return "u64";
    case 'z': return "!";
  }
  return nullptr;
}

// Parser for Rust symbol mangling v0, whose grammar is defined here:
//
// https://doc.rust-lang.org/rustc/symbol-mangling/v0.html#symbol-grammar-summary
class RustSymbolParser {
 public:
  // Prepares to demangle the given encoding, a Rust symbol name starting with
  // _R, into the output buffer [out, out_end).  The caller is expected to
  // continue by calling the new object's Parse function.
  RustSymbolParser(const char* encoding, char* out, char* const out_end)
      : encoding_(encoding), out_(out), out_end_(out_end) {
    if (out_ != out_end_) *out_ = '\0';
  }

  // Parses the constructor's encoding argument, writing output into the range
  // [out, out_end).  Returns true on success and false for input whose
  // structure was not recognized or exceeded implementation limits, such as by
  // nesting structures too deep.  In either case *this should not be used
  // again.
  ABSL_MUST_USE_RESULT bool Parse() && {
    // Recursively parses the grammar production named by callee, then resumes
    // execution at the next statement.
    //
    // Recursive-descent parsing is a beautifully readable translation of a
    // grammar, but it risks stack overflow if implemented by naive recursion on
    // the C++ call stack.  So we simulate recursion by goto and switch instead,
    // keeping a bounded stack of "return addresses" in the recursion_stack_
    // member.
    //
    // The callee argument is a statement label.  We goto that label after
    // saving the "return address" on recursion_stack_.  The next continue
    // statement in the for loop below "returns" from this "call".
    //
    // The caller argument names the return point.  Each value of caller must
    // appear in only one ABSL_DEMANGLER_RECURSE call and be listed in the
    // definition of enum ReturnAddress.  The switch implements the control
    // transfer from the end of a "called" subroutine back to the statement
    // after the "call".
    //
    // Note that not all the grammar productions have to be packed into the
    // switch, but only those which appear in a cycle in the grammar.  Anything
    // acyclic can be written as ordinary functions and function calls, e.g.,
    // ParseIdentifier.
#define ABSL_DEMANGLER_RECURSE(callee, caller) \
    do { \
      if (recursion_depth_ == kStackSize) return false; \
      /* The next continue will switch on this saved value ... */ \
      recursion_stack_[recursion_depth_++] = caller; \
      goto callee; \
      /* ... and will land here, resuming the suspended code. */ \
      case caller: {} \
    } while (0)

    // Parse the encoding, counting completed recursive calls to guard against
    // excessively complex input and infinite-loop bugs.
    int iter = 0;
    goto whole_encoding;
    for (; iter < kMaxReturns && recursion_depth_ > 0; ++iter) {
      // This switch resumes the code path most recently suspended by
      // ABSL_DEMANGLER_RECURSE.
      switch (recursion_stack_[--recursion_depth_]) {
        //
        // symbol-name ->
        // _R decimal-number? path instantiating-crate? vendor-specific-suffix?
        whole_encoding:
          if (!Eat('_') || !Eat('R')) return false;
          // decimal-number? is always empty today, so proceed to path, which
          // can't start with a decimal digit.
          ABSL_DEMANGLER_RECURSE(path, kInstantiatingCrate);
          if (IsAlpha(Peek())) {
            ++silence_depth_;  // Print nothing more from here on.
            ABSL_DEMANGLER_RECURSE(path, kVendorSpecificSuffix);
          }
          switch (Take()) {
            case '.': case '$': case '\0': return true;
          }
          return false;  // unexpected trailing content

        // path -> crate-root | inherent-impl | trait-impl | trait-definition |
        //         nested-path | generic-args | backref
        //
        // Note that ABSL_DEMANGLER_RECURSE does not work inside a nested switch
        // (which would hide the generated case label).  Thus we jump out of the
        // inner switch with gotos before performing any fake recursion.
        path:
          switch (Take()) {
            case 'C': goto crate_root;
            case 'M': goto inherent_impl;
            case 'X': goto trait_impl;
            case 'Y': goto trait_definition;
            case 'N': goto nested_path;
            case 'I': goto generic_args;
            case 'B': goto path_backref;
            default: return false;
          }

        // crate-root -> C identifier (C consumed above)
        crate_root:
          if (!ParseIdentifier()) return false;
          continue;

        // inherent-impl -> M impl-path type (M already consumed)
        inherent_impl:
          if (!Emit("<")) return false;
          ABSL_DEMANGLER_RECURSE(impl_path, kInherentImplType);
          ABSL_DEMANGLER_RECURSE(type, kInherentImplEnding);
          if (!Emit(">")) return false;
          continue;

        // trait-impl -> X impl-path type path (X already consumed)
        trait_impl:
          if (!Emit("<")) return false;
          ABSL_DEMANGLER_RECURSE(impl_path, kTraitImplType);
          ABSL_DEMANGLER_RECURSE(type, kTraitImplInfix);
          if (!Emit(" as ")) return false;
          ABSL_DEMANGLER_RECURSE(path, kTraitImplEnding);
          if (!Emit(">")) return false;
          continue;

        // impl-path -> disambiguator? path (but never print it!)
        impl_path:
          ++silence_depth_;
          {
            int ignored_disambiguator;
            if (!ParseDisambiguator(ignored_disambiguator)) return false;
          }
          ABSL_DEMANGLER_RECURSE(path, kImplPathEnding);
          --silence_depth_;
          continue;

        // trait-definition -> Y type path (Y already consumed)
        trait_definition:
          if (!Emit("<")) return false;
          ABSL_DEMANGLER_RECURSE(type, kTraitDefinitionInfix);
          if (!Emit(" as ")) return false;
          ABSL_DEMANGLER_RECURSE(path, kTraitDefinitionEnding);
          if (!Emit(">")) return false;
          continue;

        // nested-path -> N namespace path identifier (N already consumed)
        // namespace -> lower | upper
        nested_path:
          // Uppercase namespaces must be saved on a stack so we can print
          // ::{closure#0} or ::{shim:vtable#0} or ::{X:name#0} as needed.
          if (IsUpper(Peek())) {
            if (!PushNamespace(Take())) return false;
            ABSL_DEMANGLER_RECURSE(path, kIdentifierInUppercaseNamespace);
            if (!Emit("::")) return false;
            if (!ParseIdentifier(PopNamespace())) return false;
            continue;
          }

          // Lowercase namespaces, however, are never represented in the output;
          // they all emit just ::name.
          if (IsLower(Take())) {
            ABSL_DEMANGLER_RECURSE(path, kIdentifierInLowercaseNamespace);
            if (!Emit("::")) return false;
            if (!ParseIdentifier()) return false;
            continue;
          }

          // Neither upper or lower
          return false;

        // type -> basic-type | array-type | slice-type | tuple-type |
        //         ref-type | mut-ref-type | const-ptr-type | mut-ptr-type |
        //         fn-type | dyn-trait-type | path | backref
        //
        // We use ifs instead of switch (Take()) because the default case jumps
        // to path, which will need to see the first character not yet Taken
        // from the input.  Because we do not use a nested switch here,
        // ABSL_DEMANGLER_RECURSE works fine in the 'S' case.
        type:
          if (IsLower(Peek())) {
            const char* type_name = BasicTypeName(Take());
            if (type_name == nullptr || !Emit(type_name)) return false;
            continue;
          }
          if (Eat('A')) {
            // array-type = A type const
            if (!Emit("[")) return false;
            ABSL_DEMANGLER_RECURSE(type, kArraySize);
            if (!Emit("; ")) return false;
            ABSL_DEMANGLER_RECURSE(constant, kFinishArray);
            if (!Emit("]")) return false;
            continue;
          }
          if (Eat('S')) {
            if (!Emit("[")) return false;
            ABSL_DEMANGLER_RECURSE(type, kSliceEnding);
            if (!Emit("]")) return false;
            continue;
          }
          if (Eat('T')) goto tuple_type;
          if (Eat('R')) {
            if (!Emit("&")) return false;
            if (!ParseOptionalLifetime()) return false;
            goto type;
          }
          if (Eat('Q')) {
            if (!Emit("&mut ")) return false;
            if (!ParseOptionalLifetime()) return false;
            goto type;
          }
          if (Eat('P')) {
            if (!Emit("*const ")) return false;
            goto type;
          }
          if (Eat('O')) {
            if (!Emit("*mut ")) return false;
            goto type;
          }
          if (Eat('F')) goto fn_type;
          if (Eat('D')) goto dyn_trait_type;
          if (Eat('B')) goto type_backref;
          goto path;

        // tuple-type -> T type* E (T already consumed)
        tuple_type:
          if (!Emit("(")) return false;

          // The toolchain should call the unit type u instead of TE, but the
          // grammar and other demanglers also recognize TE, so we do too.
          if (Eat('E')) {
            if (!Emit(")")) return false;
            continue;
          }

          // A tuple with one element is rendered (type,) instead of (type).
          ABSL_DEMANGLER_RECURSE(type, kAfterFirstTupleElement);
          if (Eat('E')) {
            if (!Emit(",)")) return false;
            continue;
          }

          // A tuple with two elements is of course (x, y).
          if (!Emit(", ")) return false;
          ABSL_DEMANGLER_RECURSE(type, kAfterSecondTupleElement);
          if (Eat('E')) {
            if (!Emit(")")) return false;
            continue;
          }

          // And (x, y, z) for three elements.
          if (!Emit(", ")) return false;
          ABSL_DEMANGLER_RECURSE(type, kAfterThirdTupleElement);
          if (Eat('E')) {
            if (!Emit(")")) return false;
            continue;
          }

          // For longer tuples we write (x, y, z, ...), printing none of the
          // content of the fourth and later types.  Thus we avoid exhausting
          // output buffers and human readers' patience when some library has a
          // long tuple as an implementation detail, without having to
          // completely obfuscate all tuples.
          if (!Emit(", ...)")) return false;
          ++silence_depth_;
          while (!Eat('E')) {
            ABSL_DEMANGLER_RECURSE(type, kAfterSubsequentTupleElement);
          }
          --silence_depth_;
          continue;

        // fn-type -> F fn-sig (F already consumed)
        // fn-sig -> binder? U? (K abi)? type* E type
        // abi -> C | undisambiguated-identifier
        //
        // We follow the C++ demangler in suppressing details of function
        // signatures.  Every function type is rendered "fn...".
        fn_type:
          if (!Emit("fn...")) return false;
          ++silence_depth_;
          if (!ParseOptionalBinder()) return false;
          (void)Eat('U');
          if (Eat('K')) {
            if (!Eat('C') && !ParseUndisambiguatedIdentifier()) return false;
          }
          while (!Eat('E')) {
            ABSL_DEMANGLER_RECURSE(type, kContinueParameterList);
          }
          ABSL_DEMANGLER_RECURSE(type, kFinishFn);
          --silence_depth_;
          continue;

        // dyn-trait-type -> D dyn-bounds lifetime (D already consumed)
        // dyn-bounds -> binder? dyn-trait* E
        //
        // The grammar strangely allows an empty trait list, even though the
        // compiler should never output one.  We follow existing demanglers in
        // rendering DEL_ as "dyn ".
        //
        // Because auto traits lengthen a type name considerably without
        // providing much value to a search for related source code, it would be
        // desirable to abbreviate
        //     dyn main::Trait + std::marker::Copy + std::marker::Send
        // to
        //     dyn main::Trait + ...,
        // eliding the auto traits.  But it is difficult to do so correctly, in
        // part because there is no guarantee that the mangling will list the
        // main trait first.  So we just print all the traits in their order of
        // appearance in the mangled name.
        dyn_trait_type:
          if (!Emit("dyn ")) return false;
          if (!ParseOptionalBinder()) return false;
          if (!Eat('E')) {
            ABSL_DEMANGLER_RECURSE(dyn_trait, kBeginAutoTraits);
            while (!Eat('E')) {
              if (!Emit(" + ")) return false;
              ABSL_DEMANGLER_RECURSE(dyn_trait, kContinueAutoTraits);
            }
          }
          if (!ParseRequiredLifetime()) return false;
          continue;

        // dyn-trait -> path dyn-trait-assoc-binding*
        // dyn-trait-assoc-binding -> p undisambiguated-identifier type
        //
        // We render nonempty binding lists as <>, omitting their contents as
        // for generic-args.
        dyn_trait:
          ABSL_DEMANGLER_RECURSE(path, kContinueDynTrait);
          if (Peek() == 'p') {
            if (!Emit("<>")) return false;
            ++silence_depth_;
            while (Eat('p')) {
              if (!ParseUndisambiguatedIdentifier()) return false;
              ABSL_DEMANGLER_RECURSE(type, kContinueAssocBinding);
            }
            --silence_depth_;
          }
          continue;

        // const -> type const-data | p | backref
        //
        // const is a C++ keyword, so we use the label `constant` instead.
        constant:
          if (Eat('B')) goto const_backref;
          if (Eat('p')) {
            if (!Emit("_")) return false;
            continue;
          }

          // Scan the type without printing it.
          //
          // The Rust language restricts the type of a const generic argument
          // much more than the mangling grammar does.  We do not enforce this.
          //
          // We also do not bother printing false, true, 'A', and '\u{abcd}' for
          // the types bool and char.  Because we do not print generic-args
          // contents, we expect to print constants only in array sizes, and
          // those should not be bool or char.
          ++silence_depth_;
          ABSL_DEMANGLER_RECURSE(type, kConstData);
          --silence_depth_;

          // const-data -> n? hex-digit* _
          //
          // Although the grammar doesn't say this, existing demanglers expect
          // that zero is 0, not an empty digit sequence, and no nonzero value
          // may have leading zero digits.  Also n0_ is accepted and printed as
          // -0, though a toolchain will probably never write that encoding.
          if (Eat('n') && !EmitChar('-')) return false;
          if (!Emit("0x")) return false;
          if (Eat('0')) {
            if (!EmitChar('0')) return false;
            if (!Eat('_')) return false;
            continue;
          }
          while (IsLowerHexDigit(Peek())) {
            if (!EmitChar(Take())) return false;
          }
          if (!Eat('_')) return false;
          continue;

        // generic-args -> I path generic-arg* E (I already consumed)
        //
        // We follow the C++ demangler in omitting all the arguments from the
        // output, printing only the list opening and closing tokens.
        generic_args:
          ABSL_DEMANGLER_RECURSE(path, kBeginGenericArgList);
          if (!Emit("::<>")) return false;
          ++silence_depth_;
          while (!Eat('E')) {
            ABSL_DEMANGLER_RECURSE(generic_arg, kContinueGenericArgList);
          }
          --silence_depth_;
          continue;

        // generic-arg -> lifetime | type | K const
        generic_arg:
          if (Peek() == 'L') {
            if (!ParseOptionalLifetime()) return false;
            continue;
          }
          if (Eat('K')) goto constant;
          goto type;

        // backref -> B base-62-number (B already consumed)
        //
        // The BeginBackref call parses and range-checks the base-62-number.  We
        // always do that much.
        //
        // The recursive call parses and prints what the backref points at.  We
        // save CPU and stack by skipping this work if the output would be
        // suppressed anyway.
        path_backref:
          if (!BeginBackref()) return false;
          if (silence_depth_ == 0) {
            ABSL_DEMANGLER_RECURSE(path, kPathBackrefEnding);
          }
          EndBackref();
          continue;

        // This represents the same backref production as in path_backref but
        // parses the target as a type instead of a path.
        type_backref:
          if (!BeginBackref()) return false;
          if (silence_depth_ == 0) {
            ABSL_DEMANGLER_RECURSE(type, kTypeBackrefEnding);
          }
          EndBackref();
          continue;

        const_backref:
          if (!BeginBackref()) return false;
          if (silence_depth_ == 0) {
            ABSL_DEMANGLER_RECURSE(constant, kConstantBackrefEnding);
          }
          EndBackref();
          continue;
      }
    }

    return false;  // hit iteration limit or a bug in our stack handling
  }

 private:
  // Enumerates resumption points for ABSL_DEMANGLER_RECURSE calls.
  enum ReturnAddress : uint8_t {
    kInstantiatingCrate,
    kVendorSpecificSuffix,
    kIdentifierInUppercaseNamespace,
    kIdentifierInLowercaseNamespace,
    kInherentImplType,
    kInherentImplEnding,
    kTraitImplType,
    kTraitImplInfix,
    kTraitImplEnding,
    kImplPathEnding,
    kTraitDefinitionInfix,
    kTraitDefinitionEnding,
    kArraySize,
    kFinishArray,
    kSliceEnding,
    kAfterFirstTupleElement,
    kAfterSecondTupleElement,
    kAfterThirdTupleElement,
    kAfterSubsequentTupleElement,
    kContinueParameterList,
    kFinishFn,
    kBeginAutoTraits,
    kContinueAutoTraits,
    kContinueDynTrait,
    kContinueAssocBinding,
    kConstData,
    kBeginGenericArgList,
    kContinueGenericArgList,
    kPathBackrefEnding,
    kTypeBackrefEnding,
    kConstantBackrefEnding,
  };

  // Element counts for the stack arrays.  Larger stack sizes accommodate more
  // deeply nested names at the cost of a larger footprint on the C++ call
  // stack.
  enum {
    // Maximum recursive calls outstanding at one time.
    kStackSize = 256,

    // Maximum N<uppercase> nested-paths open at once.  We do not expect
    // closures inside closures inside closures as much as functions inside
    // modules inside other modules, so we can use a smaller array here.
    kNamespaceStackSize = 64,

    // Maximum number of nested backrefs.  We can keep this stack pretty small
    // because we do not follow backrefs inside generic-args or other contexts
    // that suppress printing, so deep stacking is unlikely in practice.
    kPositionStackSize = 16,
  };

  // Returns the next input character without consuming it.
  char Peek() const { return encoding_[pos_]; }

  // Consumes and returns the next input character.
  char Take() { return encoding_[pos_++]; }

  // If the next input character is the given character, consumes it and returns
  // true; otherwise returns false without consuming a character.
  ABSL_MUST_USE_RESULT bool Eat(char want) {
    if (encoding_[pos_] != want) return false;
    ++pos_;
    return true;
  }

  // Provided there is enough remaining output space, appends c to the output,
  // writing a fresh NUL terminator afterward, and returns true.  Returns false
  // if the output buffer had less than two bytes free.
  ABSL_MUST_USE_RESULT bool EmitChar(char c) {
    if (silence_depth_ > 0) return true;
    if (out_end_ - out_ < 2) return false;
    *out_++ = c;
    *out_ = '\0';
    return true;
  }

  // Provided there is enough remaining output space, appends the C string token
  // to the output, followed by a NUL character, and returns true.  Returns
  // false if not everything fit into the output buffer.
  ABSL_MUST_USE_RESULT bool Emit(const char* token) {
    if (silence_depth_ > 0) return true;
    const size_t token_length = std::strlen(token);
    const size_t bytes_to_copy = token_length + 1;  // token and final NUL
    if (static_cast<size_t>(out_end_ - out_) < bytes_to_copy) return false;
    std::memcpy(out_, token, bytes_to_copy);
    out_ += token_length;
    return true;
  }

  // Provided there is enough remaining output space, appends the decimal form
  // of disambiguator (if it's nonnegative) or "?" (if it's negative) to the
  // output, followed by a NUL character, and returns true.  Returns false if
  // not everything fit into the output buffer.
  ABSL_MUST_USE_RESULT bool EmitDisambiguator(int disambiguator) {
    if (disambiguator < 0) return EmitChar('?');  // parsed but too large
    if (disambiguator == 0) return EmitChar('0');
    // Convert disambiguator to decimal text.  Three digits per byte is enough
    // because 999 > 256.  The bound will remain correct even if future
    // maintenance changes the type of the disambiguator variable.
    char digits[3 * sizeof(disambiguator)] = {};
    size_t leading_digit_index = sizeof(digits) - 1;
    for (; disambiguator > 0; disambiguator /= 10) {
      digits[--leading_digit_index] =
          static_cast<char>('0' + disambiguator % 10);
    }
    return Emit(digits + leading_digit_index);
  }

  // Consumes an optional disambiguator (s123_) from the input.
  //
  // On success returns true and fills value with the encoded value if it was
  // not too big, otherwise with -1.  If the optional disambiguator was omitted,
  // value is 0.  On parse failure returns false and sets value to -1.
  ABSL_MUST_USE_RESULT bool ParseDisambiguator(int& value) {
    value = -1;

    // disambiguator = s base-62-number
    //
    // Disambiguators are optional.  An omitted disambiguator is zero.
    if (!Eat('s')) {
      value = 0;
      return true;
    }
    int base_62_value = 0;
    if (!ParseBase62Number(base_62_value)) return false;
    value = base_62_value < 0 ? -1 : base_62_value + 1;
    return true;
  }

  // Consumes a base-62 number like _ or 123_ from the input.
  //
  // On success returns true and fills value with the encoded value if it was
  // not too big, otherwise with -1.  On parse failure returns false and sets
  // value to -1.
  ABSL_MUST_USE_RESULT bool ParseBase62Number(int& value) {
    value = -1;

    // base-62-number = (digit | lower | upper)* _
    //
    // An empty base-62 digit sequence means 0.
    if (Eat('_')) {
      value = 0;
      return true;
    }

    // A nonempty digit sequence denotes its base-62 value plus 1.
    int encoded_number = 0;
    bool overflowed = false;
    while (IsAlpha(Peek()) || IsDigit(Peek())) {
      const char c = Take();
      if (encoded_number >= std::numeric_limits<int>::max()/62) {
        // If we are close to overflowing an int, keep parsing but stop updating
        // encoded_number and remember to return -1 at the end.  The point is to
        // avoid undefined behavior while parsing crate-root disambiguators,
        // which are large in practice but not shown in demangling, while
        // successfully computing closure and shim disambiguators, which are
        // typically small and are printed out.
        overflowed = true;
      } else {
        int digit;
        if (IsDigit(c)) {
          digit = c - '0';
        } else if (IsLower(c)) {
          digit = c - 'a' + 10;
        } else {
          digit = c - 'A' + 36;
        }
        encoded_number = 62 * encoded_number + digit;
      }
    }

    if (!Eat('_')) return false;
    if (!overflowed) value = encoded_number + 1;
    return true;
  }

  // Consumes an identifier from the input, returning true on success.
  //
  // A nonzero uppercase_namespace specifies the character after the N in a
  // nested-identifier, e.g., 'C' for a closure, allowing ParseIdentifier to
  // write out the name with the conventional decoration for that namespace.
  ABSL_MUST_USE_RESULT bool ParseIdentifier(char uppercase_namespace = '\0') {
    // identifier -> disambiguator? undisambiguated-identifier
    int disambiguator = 0;
    if (!ParseDisambiguator(disambiguator)) return false;

    return ParseUndisambiguatedIdentifier(uppercase_namespace, disambiguator);
  }

  // Consumes from the input an identifier with no preceding disambiguator,
  // returning true on success.
  //
  // When ParseIdentifier calls this, it passes the N<namespace> character and
  // disambiguator value so that "{closure#42}" and similar forms can be
  // rendered correctly.
  //
  // At other appearances of undisambiguated-identifier in the grammar, this
  // treatment is not applicable, and the call site omits both arguments.
  ABSL_MUST_USE_RESULT bool ParseUndisambiguatedIdentifier(
      char uppercase_namespace = '\0', int disambiguator = 0) {
    // undisambiguated-identifier -> u? decimal-number _? bytes
    const bool is_punycoded = Eat('u');
    if (!IsDigit(Peek())) return false;
    int num_bytes = 0;
    if (!ParseDecimalNumber(num_bytes)) return false;
    (void)Eat('_');  // optional separator, needed if a digit follows
    if (is_punycoded) {
      DecodeRustPunycodeOptions options;
      options.punycode_begin = &encoding_[pos_];
      options.punycode_end = &encoding_[pos_] + num_bytes;
      options.out_begin = out_;
      options.out_end = out_end_;
      out_ = DecodeRustPunycode(options);
      if (out_ == nullptr) return false;
      pos_ += static_cast<size_t>(num_bytes);
    }

    // Emit the beginnings of braced forms like {shim:vtable#0}.
    if (uppercase_namespace != '\0') {
      switch (uppercase_namespace) {
        case 'C':
          if (!Emit("{closure")) return false;
          break;
        case 'S':
          if (!Emit("{shim")) return false;
          break;
        default:
          if (!EmitChar('{') || !EmitChar(uppercase_namespace)) return false;
          break;
      }
      if (num_bytes > 0 && !Emit(":")) return false;
    }

    // Emit the name itself.
    if (!is_punycoded) {
      for (int i = 0; i < num_bytes; ++i) {
        const char c = Take();
        if (!IsIdentifierChar(c) &&
            // The spec gives toolchains the choice of Punycode or raw UTF-8 for
            // identifiers containing code points above 0x7f, so accept bytes
            // with the high bit set.
            (c & 0x80) == 0) {
          return false;
        }
        if (!EmitChar(c)) return false;
      }
    }

    // Emit the endings of braced forms, e.g., "#42}".
    if (uppercase_namespace != '\0') {
      if (!EmitChar('#')) return false;
      if (!EmitDisambiguator(disambiguator)) return false;
      if (!EmitChar('}')) return false;
    }

    return true;
  }

  // Consumes a decimal number like 0 or 123 from the input.  On success returns
  // true and fills value with the encoded value.  If the encoded value is too
  // large or otherwise unparsable, returns false and sets value to -1.
  ABSL_MUST_USE_RESULT bool ParseDecimalNumber(int& value) {
    value = -1;
    if (!IsDigit(Peek())) return false;
    int encoded_number = Take() - '0';
    if (encoded_number == 0) {
      // Decimal numbers are never encoded with extra leading zeroes.
      value = 0;
      return true;
    }
    while (IsDigit(Peek()) &&
           // avoid overflow
           encoded_number < std::numeric_limits<int>::max()/10) {
      encoded_number = 10 * encoded_number + (Take() - '0');
    }
    if (IsDigit(Peek())) return false;  // too big
    value = encoded_number;
    return true;
  }

  // Consumes a binder of higher-ranked lifetimes if one is present.  On success
  // returns true and discards the encoded lifetime count.  On parse failure
  // returns false.
  ABSL_MUST_USE_RESULT bool ParseOptionalBinder() {
    // binder -> G base-62-number
    if (!Eat('G')) return true;
    int ignored_binding_count;
    return ParseBase62Number(ignored_binding_count);
  }

  // Consumes a lifetime if one is present.
  //
  // On success returns true and discards the lifetime index.  We do not print
  // or even range-check lifetimes because they are a finer detail than other
  // things we omit from output, such as the entire contents of generic-args.
  //
  // On parse failure returns false.
  ABSL_MUST_USE_RESULT bool ParseOptionalLifetime() {
    // lifetime -> L base-62-number
    if (!Eat('L')) return true;
    int ignored_de_bruijn_index;
    return ParseBase62Number(ignored_de_bruijn_index);
  }

  // Consumes a lifetime just like ParseOptionalLifetime, but returns false if
  // there is no lifetime here.
  ABSL_MUST_USE_RESULT bool ParseRequiredLifetime() {
    if (Peek() != 'L') return false;
    return ParseOptionalLifetime();
  }

  // Pushes ns onto the namespace stack and returns true if the stack is not
  // full, else returns false.
  ABSL_MUST_USE_RESULT bool PushNamespace(char ns) {
    if (namespace_depth_ == kNamespaceStackSize) return false;
    namespace_stack_[namespace_depth_++] = ns;
    return true;
  }

  // Pops the last pushed namespace.  Requires that the namespace stack is not
  // empty (namespace_depth_ > 0).
  char PopNamespace() { return namespace_stack_[--namespace_depth_]; }

  // Pushes position onto the position stack and returns true if the stack is
  // not full, else returns false.
  ABSL_MUST_USE_RESULT bool PushPosition(int position) {
    if (position_depth_ == kPositionStackSize) return false;
    position_stack_[position_depth_++] = position;
    return true;
  }

  // Pops the last pushed input position.  Requires that the position stack is
  // not empty (position_depth_ > 0).
  int PopPosition() { return position_stack_[--position_depth_]; }

  // Consumes a base-62-number denoting a backref target, pushes the current
  // input position on the data stack, and sets the input position to the
  // beginning of the backref target.  Returns true on success.  Returns false
  // if parsing failed, the stack is exhausted, or the backref target position
  // is out of range.
  ABSL_MUST_USE_RESULT bool BeginBackref() {
    // backref = B base-62-number (B already consumed)
    //
    // Reject backrefs that don't parse, overflow int, or don't point backward.
    // If the offset looks fine, adjust it to account for the _R prefix.
    int offset = 0;
    const int offset_of_this_backref =
        pos_ - 2 /* _R */ - 1 /* B already consumed */;
    if (!ParseBase62Number(offset) || offset < 0 ||
        offset >= offset_of_this_backref) {
      return false;
    }
    offset += 2;

    // Save the old position to restore later.
    if (!PushPosition(pos_)) return false;

    // Move the input position to the backref target.
    //
    // Note that we do not check whether the new position points to the
    // beginning of a construct matching the context in which the backref
    // appeared.  We just jump to it and see whether nested parsing succeeds.
    // We therefore accept various wrong manglings, e.g., a type backref
    // pointing to an 'l' character inside an identifier, which happens to mean
    // i32 when parsed as a type mangling.  This saves the complexity and RAM
    // footprint of remembering which offsets began which kinds of
    // substructures.  Existing demanglers take similar shortcuts.
    pos_ = offset;
    return true;
  }

  // Cleans up after a backref production by restoring the previous input
  // position from the data stack.
  void EndBackref() { pos_ = PopPosition(); }

  // The leftmost recursion_depth_ elements of recursion_stack_ contain the
  // ReturnAddresses pushed by ABSL_DEMANGLER_RECURSE calls not yet completed.
  ReturnAddress recursion_stack_[kStackSize] = {};
  int recursion_depth_ = 0;

  // The leftmost namespace_depth_ elements of namespace_stack_ contain the
  // uppercase namespace identifiers for open nested-paths, e.g., 'C' for a
  // closure.
  char namespace_stack_[kNamespaceStackSize] = {};
  int namespace_depth_ = 0;

  // The leftmost position_depth_ elements of position_stack_ contain the input
  // positions to return to after fully printing the targets of backrefs.
  int position_stack_[kPositionStackSize] = {};
  int position_depth_ = 0;

  // Anything parsed while silence_depth_ > 0 contributes nothing to the
  // demangled output.  For constructs omitted from the demangling, such as
  // impl-path and the contents of generic-args, we will increment
  // silence_depth_ on the way in and decrement silence_depth_ on the way out.
  int silence_depth_ = 0;

  // Input: encoding_ points to a Rust mangled symbol, and encoding_[pos_] is
  // the next input character to be scanned.
  int pos_ = 0;
  const char* encoding_ = nullptr;

  // Output: *out_ is where the next output character should be written, and
  // out_end_ points past the last byte of available space.
  char* out_ = nullptr;
  char* out_end_ = nullptr;
};

}  // namespace

bool DemangleRustSymbolEncoding(const char* mangled, char* out,
                                size_t out_size) {
  return RustSymbolParser(mangled, out, out + out_size).Parse();
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
