// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATOR_H_
#define V8_COMPILER_OPERATOR_H_

#include <ostream>

#include "src/base/compiler-specific.h"
#include "src/base/flags.h"
#include "src/base/hashing.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// An operator represents description of the "computation" of a node in the
// compiler IR. A computation takes values (i.e. data) as input and produces
// zero or more values as output. The side-effects of a computation must be
// captured by additional control and data dependencies which are part of the
// IR graph.
// Operators are immutable and describe the statically-known parts of a
// computation. Thus they can be safely shared by many different nodes in the
// IR graph, or even globally between graphs. Operators can have "static
// parameters" which are compile-time constant parameters to the operator, such
// as the name for a named field access, the ID of a runtime function, etc.
// Static parameters are private to the operator and only semantically
// meaningful to the operator itself.
class V8_EXPORT_PRIVATE Operator : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  using Opcode = uint16_t;

  // Properties inform the operator-independent optimizer about legal
  // transformations for nodes that have this operator.
  enum Property {
    kNoProperties = 0,
    kCommutative = 1 << 0,  // OP(a, b) == OP(b, a) for all inputs.
    kAssociative = 1 << 1,  // OP(a, OP(b,c)) == OP(OP(a,b), c) for all inputs.
    kIdempotent = 1 << 2,   // OP(a); OP(a) == OP(a).
    kNoRead = 1 << 3,       // Has no scheduling dependency on Effects
    kNoWrite = 1 << 4,      // Does not modify any Effects and thereby
                            // create new scheduling dependencies.
    kNoThrow = 1 << 5,      // Can never generate an exception.
    kNoDeopt = 1 << 6,      // Can never generate an eager deoptimization exit.
    kFoldable = kNoRead | kNoWrite,
    kEliminatable = kNoDeopt | kNoWrite | kNoThrow,
    kKontrol = kNoDeopt | kFoldable | kNoThrow,
    kPure = kKontrol | kIdempotent
  };

// List of all bits, for the visualizer.
#define OPERATOR_PROPERTY_LIST(V) \
  V(Commutative)                  \
  V(Associative) V(Idempotent) V(NoRead) V(NoWrite) V(NoThrow) V(NoDeopt)

  using Properties = base::Flags<Property, uint8_t>;
  enum class PrintVerbosity { kVerbose, kSilent };

  // Constructor.
  Operator(Opcode opcode, Properties properties, const char* mnemonic,
           size_t value_in, size_t effect_in, size_t control_in,
           size_t value_out, size_t effect_out, size_t control_out);
  Operator(const Operator&) = delete;
  Operator& operator=(const Operator&) = delete;

  virtual ~Operator() = default;

  // A small integer unique to all instances of a particular kind of operator,
  // useful for quick matching for specific kinds of operators. For fast access
  // the opcode is stored directly in the operator object.
  constexpr Opcode opcode() const { return opcode_; }

  // Returns a constant string representing the mnemonic of the operator,
  // without the static parameters. Useful for debugging.
  const char* mnemonic() const { return mnemonic_; }

  // Check if this operator equals another operator. Equivalent operators can
  // be merged, and nodes with equivalent operators and equivalent inputs
  // can be merged.
  virtual bool Equals(const Operator* that) const {
    return this->opcode() == that->opcode();
  }

  // Compute a hashcode to speed up equivalence-set checking.
  // Equal operators should always have equal hashcodes, and unequal operators
  // should have unequal hashcodes with high probability.
  virtual size_t HashCode() const { return base::hash<Opcode>()(opcode()); }

  // Check whether this operator has the given property.
  bool HasProperty(Property property) const {
    return (properties() & property) == property;
  }

  Properties properties() const { return properties_; }

  // TODO(titzer): convert return values here to size_t.
  int ValueInputCount() const { return value_in_; }
  int EffectInputCount() const { return effect_in_; }
  int ControlInputCount() const { return control_in_; }

  int ValueOutputCount() const { return value_out_; }
  int EffectOutputCount() const { return effect_out_; }
  int ControlOutputCount() const { return control_out_; }

  static size_t ZeroIfEliminatable(Properties properties) {
    return (properties & kEliminatable) == kEliminatable ? 0 : 1;
  }

  static size_t ZeroIfNoThrow(Properties properties) {
    return (properties & kNoThrow) == kNoThrow ? 0 : 2;
  }

  static size_t ZeroIfPure(Properties properties) {
    return (properties & kPure) == kPure ? 0 : 1;
  }

  // TODO(titzer): API for input and output types, for typechecking graph.

  // Print the full operator into the given stream, including any
  // static parameters. Useful for debugging and visualizing the IR.
  void PrintTo(std::ostream& os,
               PrintVerbosity verbose = PrintVerbosity::kVerbose) const {
    // We cannot make PrintTo virtual, because default arguments to virtual
    // methods are banned in the style guide.
    return PrintToImpl(os, verbose);
  }

  void PrintPropsTo(std::ostream& os) const;

 protected:
  virtual void PrintToImpl(std::ostream& os, PrintVerbosity verbose) const;

 private:
  const char* mnemonic_;
  Opcode opcode_;
  Properties properties_;
  uint32_t value_in_;
  uint32_t effect_in_;
  uint32_t control_in_;
  uint32_t value_out_;
  uint8_t effect_out_;
  uint32_t control_out_;
};

DEFINE_OPERATORS_FOR_FLAGS(Operator::Properties)

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const Operator& op);

// Default equality function for below Operator1<*> class.
template <typename T>
struct OpEqualTo : public std::equal_to<T> {};


// Default hashing function for below Operator1<*> class.
template <typename T>
struct OpHash : public base::hash<T> {};


// A templatized implementation of Operator that has one static parameter of
// type {T} with the proper default equality and hashing functions.
template <typename T, typename Pred = OpEqualTo<T>, typename Hash = OpHash<T>>
class Operator1 : public Operator {
 public:
  Operator1(Opcode opcode, Properties properties, const char* mnemonic,
            size_t value_in, size_t effect_in, size_t control_in,
            size_t value_out, size_t effect_out, size_t control_out,
            T parameter, Pred const& pred = Pred(), Hash const& hash = Hash())
      : Operator(opcode, properties, mnemonic, value_in, effect_in, control_in,
                 value_out, effect_out, control_out),
        parameter_(parameter),
        pred_(pred),
        hash_(hash) {}

  T const& parameter() const { return parameter_; }

  bool Equals(const Operator* other) const final {
    if (opcode() != other->opcode()) return false;
    const Operator1<T, Pred, Hash>* that =
        reinterpret_cast<const Operator1<T, Pred, Hash>*>(other);
    return this->pred_(this->parameter(), that->parameter());
  }
  size_t HashCode() const final {
    return base::hash_combine(this->opcode(), this->hash_(this->parameter()));
  }
  // For most parameter types, we have only a verbose way to print them, namely
  // ostream << parameter. But for some types it is particularly useful to have
  // a shorter way to print them for the node labels in Turbolizer. The
  // following method can be overridden to provide a concise and a verbose
  // printing of a parameter.

  virtual void PrintParameter(std::ostream& os, PrintVerbosity verbose) const {
    os << "[" << parameter() << "]";
  }

  void PrintToImpl(std::ostream& os, PrintVerbosity verbose) const override {
    os << mnemonic();
    PrintParameter(os, verbose);
  }

 private:
  T const parameter_;
  Pred const pred_;
  Hash const hash_;
};


// Helper to extract parameters from Operator1<*> operator.
template <typename T>
inline T const& OpParameter(const Operator* op) {
  return reinterpret_cast<const Operator1<T, OpEqualTo<T>, OpHash<T>>*>(op)
      ->parameter();
}


// NOTE: We have to be careful to use the right equal/hash functions below, for
// float/double we always use the ones operating on the bit level, for Handle<>
// we always use the ones operating on the location level.
template <>
struct OpEqualTo<float> : public base::bit_equal_to<float> {};
template <>
struct OpHash<float> : public base::bit_hash<float> {};

template <>
struct OpEqualTo<double> : public base::bit_equal_to<double> {};
template <>
struct OpHash<double> : public base::bit_hash<double> {};

template <class T>
struct OpEqualTo<IndirectHandle<T>> : public IndirectHandle<T>::equal_to {};
template <class T>
struct OpHash<IndirectHandle<T>> : public IndirectHandle<T>::hash {};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_OPERATOR_H_
