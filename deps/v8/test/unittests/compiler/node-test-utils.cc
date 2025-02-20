// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/node-test-utils.h"

#include <vector>

#include "src/compiler/common-operator.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/handles/handles-inl.h"

using testing::_;
using testing::MakeMatcher;
using testing::MatcherInterface;
using testing::MatchResultListener;
using testing::StringMatchResultListener;

namespace v8 {
namespace internal {

bool operator==(DirectHandle<HeapObject> const& lhs,
                DirectHandle<HeapObject> const& rhs) {
  return lhs.is_identical_to(rhs);
}

namespace compiler {

namespace {

template <typename T>
bool PrintMatchAndExplain(const T& value, const std::string& value_name,
                          const Matcher<T>& value_matcher,
                          MatchResultListener* listener) {
  StringMatchResultListener value_listener;
  if (!value_matcher.MatchAndExplain(value, &value_listener)) {
    *listener << "whose " << value_name << " " << value << " doesn't match";
    if (value_listener.str() != "") {
      *listener << ", " << value_listener.str();
    }
    return false;
  }
  return true;
}

class TestNodeMatcher : public MatcherInterface<Node*> {
 public:
  explicit TestNodeMatcher(IrOpcode::Value opcode) : opcode_(opcode) {}

  void DescribeTo(std::ostream* os) const override {
    *os << "is a " << IrOpcode::Mnemonic(opcode_) << " node";
  }

  bool MatchAndExplain(Node* node,
                       MatchResultListener* listener) const override {
    if (node == nullptr) {
      *listener << "which is NULL";
      return false;
    }
    if (node->opcode() != opcode_) {
      *listener << "whose opcode is " << IrOpcode::Mnemonic(node->opcode())
                << " but should have been " << IrOpcode::Mnemonic(opcode_);
      return false;
    }
    return true;
  }

 private:
  const IrOpcode::Value opcode_;
};

class IsBranchMatcher final : public TestNodeMatcher {
 public:
  IsBranchMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kBranch),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsLoopExitValueMatcher final : public TestNodeMatcher {
 public:
  IsLoopExitValueMatcher(const Matcher<MachineRepresentation>& rep_matcher,
                         const Matcher<Node*>& value_matcher)
      : TestNodeMatcher(IrOpcode::kLoopExitValue),
        rep_matcher_(rep_matcher),
        value_matcher_(value_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << ") whose rep (";
    rep_matcher_.DescribeTo(os);
    *os << " and value (";
    value_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(LoopExitValueRepresentationOf(node->op()),
                                 "representation", rep_matcher_, listener)) &&
           PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "value",
                                value_matcher_, listener);
  }

 private:
  const Matcher<MachineRepresentation> rep_matcher_;
  const Matcher<Node*> value_matcher_;
};

class IsSwitchMatcher final : public TestNodeMatcher {
 public:
  IsSwitchMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kSwitch),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsIfValueMatcher final : public TestNodeMatcher {
 public:
  IsIfValueMatcher(const Matcher<IfValueParameters>& value_matcher,
                   const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kIfValue),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(IfValueParametersOf(node->op()), "value",
                                 value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<IfValueParameters> value_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsControl1Matcher final : public TestNodeMatcher {
 public:
  IsControl1Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(opcode), control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> control_matcher_;
};

class IsControl2Matcher final : public TestNodeMatcher {
 public:
  IsControl2Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control0_matcher,
                    const Matcher<Node*>& control1_matcher)
      : TestNodeMatcher(opcode),
        control0_matcher_(control0_matcher),
        control1_matcher_(control1_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose control0 (";
    control0_matcher_.DescribeTo(os);
    *os << ") and control1 (";
    control1_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 0),
                                 "control0", control0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 1),
                                 "control1", control1_matcher_, listener));
  }

 private:
  const Matcher<Node*> control0_matcher_;
  const Matcher<Node*> control1_matcher_;
};

class IsControl3Matcher final : public TestNodeMatcher {
 public:
  IsControl3Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control0_matcher,
                    const Matcher<Node*>& control1_matcher,
                    const Matcher<Node*>& control2_matcher)
      : TestNodeMatcher(opcode),
        control0_matcher_(control0_matcher),
        control1_matcher_(control1_matcher),
        control2_matcher_(control2_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose control0 (";
    control0_matcher_.DescribeTo(os);
    *os << ") and control1 (";
    control1_matcher_.DescribeTo(os);
    *os << ") and control2 (";
    control2_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 0),
                                 "control0", control0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 1),
                                 "control1", control1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 2),
                                 "control2", control2_matcher_, listener));
  }

 private:
  const Matcher<Node*> control0_matcher_;
  const Matcher<Node*> control1_matcher_;
  const Matcher<Node*> control2_matcher_;
};

class IsBeginRegionMatcher final : public TestNodeMatcher {
 public:
  explicit IsBeginRegionMatcher(const Matcher<Node*>& effect_matcher)
      : TestNodeMatcher(IrOpcode::kBeginRegion),
        effect_matcher_(effect_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose effect (";
    effect_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener));
  }

 private:
  const Matcher<Node*> effect_matcher_;
};

class IsFinishRegionMatcher final : public TestNodeMatcher {
 public:
  IsFinishRegionMatcher(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher)
      : TestNodeMatcher(IrOpcode::kFinishRegion),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and effect (";
    effect_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
};

class IsReturnMatcher final : public TestNodeMatcher {
 public:
  IsReturnMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& effect_matcher,
                  const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kReturn),
        value_matcher_(value_matcher),
        value2_matcher_(_),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher),
        has_second_return_value_(false) {}

  IsReturnMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& value2_matcher,
                  const Matcher<Node*>& effect_matcher,
                  const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kReturn),
        value_matcher_(value_matcher),
        value2_matcher_(value2_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher),
        has_second_return_value_(true) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    if (has_second_return_value_) {
      *os << ") and second value (";
      value2_matcher_.DescribeTo(os);
    }
    *os << ") and effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value", value_matcher_, listener) &&
            (!has_second_return_value_ ||
             PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                  "value2", value2_matcher_, listener)) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> value2_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
  bool has_second_return_value_;
};

class IsTerminateMatcher final : public TestNodeMatcher {
 public:
  IsTerminateMatcher(const Matcher<Node*>& effect_matcher,
                     const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kTerminate),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsTypeGuardMatcher final : public TestNodeMatcher {
 public:
  IsTypeGuardMatcher(const Matcher<Node*>& value_matcher,
                     const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kTypeGuard),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> control_matcher_;
};

template <typename T>
class IsConstantMatcher final : public TestNodeMatcher {
 public:
  IsConstantMatcher(IrOpcode::Value opcode, const Matcher<T>& value_matcher)
      : TestNodeMatcher(opcode), value_matcher_(value_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<T>(node->op()), "value",
                                 value_matcher_, listener));
  }

 private:
  const Matcher<T> value_matcher_;
};

class IsSelectMatcher final : public TestNodeMatcher {
 public:
  IsSelectMatcher(const Matcher<MachineRepresentation>& type_matcher,
                  const Matcher<Node*>& value0_matcher,
                  const Matcher<Node*>& value1_matcher,
                  const Matcher<Node*>& value2_matcher)
      : TestNodeMatcher(IrOpcode::kSelect),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        value2_matcher_(value2_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose representation (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << ") and value2 (";
    value2_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (
        TestNodeMatcher::MatchAndExplain(node, listener) &&
        PrintMatchAndExplain(SelectParametersOf(node->op()).representation(),
                             "representation", type_matcher_, listener) &&
        PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "value0",
                             value0_matcher_, listener) &&
        PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "value1",
                             value1_matcher_, listener) &&
        PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2), "value2",
                             value2_matcher_, listener));
  }

 private:
  const Matcher<MachineRepresentation> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> value2_matcher_;
};

class IsPhiMatcher final : public TestNodeMatcher {
 public:
  IsPhiMatcher(const Matcher<MachineRepresentation>& type_matcher,
               const Matcher<Node*>& value0_matcher,
               const Matcher<Node*>& value1_matcher,
               const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kPhi),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose representation (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(PhiRepresentationOf(node->op()),
                                 "representation", type_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value0", value0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value1", value1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<MachineRepresentation> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsPhi2Matcher final : public TestNodeMatcher {
 public:
  IsPhi2Matcher(const Matcher<MachineRepresentation>& type_matcher,
                const Matcher<Node*>& value0_matcher,
                const Matcher<Node*>& value1_matcher,
                const Matcher<Node*>& value2_matcher,
                const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kPhi),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        value2_matcher_(value2_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose representation (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << "), value2 (";
    value2_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(PhiRepresentationOf(node->op()),
                                 "representation", type_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value0", value0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value1", value1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value2", value2_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<MachineRepresentation> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> value2_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsEffectPhiMatcher final : public TestNodeMatcher {
 public:
  IsEffectPhiMatcher(const Matcher<Node*>& effect0_matcher,
                     const Matcher<Node*>& effect1_matcher,
                     const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kEffectPhi),
        effect0_matcher_(effect0_matcher),
        effect1_matcher_(effect1_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << "), effect0 (";
    effect0_matcher_.DescribeTo(os);
    *os << "), effect1 (";
    effect1_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node, 0),
                                 "effect0", effect0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node, 1),
                                 "effect1", effect1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> effect0_matcher_;
  const Matcher<Node*> effect1_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsProjectionMatcher final : public TestNodeMatcher {
 public:
  IsProjectionMatcher(const Matcher<size_t>& index_matcher,
                      const Matcher<Node*>& base_matcher)
      : TestNodeMatcher(IrOpcode::kProjection),
        index_matcher_(index_matcher),
        base_matcher_(base_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose index (";
    index_matcher_.DescribeTo(os);
    *os << ") and base (";
    base_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<size_t>(node->op()), "index",
                                 index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener));
  }

 private:
  const Matcher<size_t> index_matcher_;
  const Matcher<Node*> base_matcher_;
};

class IsCallMatcher final : public TestNodeMatcher {
 public:
  IsCallMatcher(const Matcher<const CallDescriptor*>& descriptor_matcher,
                const std::vector<Matcher<Node*>>& value_matchers,
                const Matcher<Node*>& effect_matcher,
                const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kCall),
        descriptor_matcher_(descriptor_matcher),
        value_matchers_(value_matchers),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      if (i == 0) {
        *os << " whose value0 (";
      } else {
        *os << "), value" << i << " (";
      }
      value_matchers_[i].DescribeTo(os);
    }
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    if (!TestNodeMatcher::MatchAndExplain(node, listener) ||
        !PrintMatchAndExplain(CallDescriptorOf(node->op()), "descriptor",
                              descriptor_matcher_, listener)) {
      return false;
    }
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      std::ostringstream ost;
      ost << "value" << i;
      if (!PrintMatchAndExplain(
              NodeProperties::GetValueInput(node, static_cast<int>(i)),
              ost.str(), value_matchers_[i], listener)) {
        return false;
      }
    }
    Node* effect_node = nullptr;
    Node* control_node = nullptr;
    if (NodeProperties::FirstEffectIndex(node) < node->InputCount()) {
      effect_node = NodeProperties::GetEffectInput(node);
    }
    if (NodeProperties::FirstControlIndex(node) < node->InputCount()) {
      control_node = NodeProperties::GetControlInput(node);
    }
    return (PrintMatchAndExplain(effect_node, "effect", effect_matcher_,
                                 listener) &&
            PrintMatchAndExplain(control_node, "control", control_matcher_,
                                 listener));
  }

 private:
  const Matcher<const CallDescriptor*> descriptor_matcher_;
  const std::vector<Matcher<Node*>> value_matchers_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsTailCallMatcher final : public TestNodeMatcher {
 public:
  IsTailCallMatcher(const Matcher<CallDescriptor const*>& descriptor_matcher,
                    const std::vector<Matcher<Node*>>& value_matchers,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kTailCall),
        descriptor_matcher_(descriptor_matcher),
        value_matchers_(value_matchers),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      if (i == 0) {
        *os << " whose value0 (";
      } else {
        *os << "), value" << i << " (";
      }
      value_matchers_[i].DescribeTo(os);
    }
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    if (!TestNodeMatcher::MatchAndExplain(node, listener) ||
        !PrintMatchAndExplain(CallDescriptorOf(node->op()), "descriptor",
                              descriptor_matcher_, listener)) {
      return false;
    }
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      std::ostringstream ost;
      ost << "value" << i;
      if (!PrintMatchAndExplain(
              NodeProperties::GetValueInput(node, static_cast<int>(i)),
              ost.str(), value_matchers_[i], listener)) {
        return false;
      }
    }
    Node* effect_node = nullptr;
    Node* control_node = nullptr;
    if (NodeProperties::FirstEffectIndex(node) < node->InputCount()) {
      effect_node = NodeProperties::GetEffectInput(node);
    }
    if (NodeProperties::FirstControlIndex(node) < node->InputCount()) {
      control_node = NodeProperties::GetControlInput(node);
    }
    return (PrintMatchAndExplain(effect_node, "effect", effect_matcher_,
                                 listener) &&
            PrintMatchAndExplain(control_node, "control", control_matcher_,
                                 listener));
  }

 private:
  const Matcher<CallDescriptor const*> descriptor_matcher_;
  const std::vector<Matcher<Node*>> value_matchers_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsSpeculativeBinopMatcher final : public TestNodeMatcher {
 public:
  IsSpeculativeBinopMatcher(IrOpcode::Value opcode,
                            const Matcher<NumberOperationHint>& hint_matcher,
                            const Matcher<Node*>& lhs_matcher,
                            const Matcher<Node*>& rhs_matcher,
                            const Matcher<Node*>& effect_matcher,
                            const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(opcode),
        hint_matcher_(hint_matcher),
        lhs_matcher_(lhs_matcher),
        rhs_matcher_(rhs_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            // TODO(bmeurer): The type parameter is currently ignored.
            PrintMatchAndExplain(NumberOperationHintOf(node->op()), "hints",
                                 hint_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "rhs",
                                 rhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<NumberOperationHint> hint_matcher_;
  const Matcher<Type> type_matcher_;
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> rhs_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsAllocateMatcher final : public TestNodeMatcher {
 public:
  IsAllocateMatcher(const Matcher<Node*>& size_matcher,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kAllocate),
        size_matcher_(size_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "size",
                                 size_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> size_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsLoadFieldMatcher final : public TestNodeMatcher {
 public:
  IsLoadFieldMatcher(const Matcher<FieldAccess>& access_matcher,
                     const Matcher<Node*>& base_matcher,
                     const Matcher<Node*>& effect_matcher,
                     const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kLoadField),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(FieldAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<FieldAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsStoreFieldMatcher final : public TestNodeMatcher {
 public:
  IsStoreFieldMatcher(const Matcher<FieldAccess>& access_matcher,
                      const Matcher<Node*>& base_matcher,
                      const Matcher<Node*>& value_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kStoreField),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(FieldAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<FieldAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsLoadElementMatcher final : public TestNodeMatcher {
 public:
  IsLoadElementMatcher(const Matcher<ElementAccess>& access_matcher,
                       const Matcher<Node*>& base_matcher,
                       const Matcher<Node*>& index_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kLoadElement),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(ElementAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<ElementAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsStoreElementMatcher final : public TestNodeMatcher {
 public:
  IsStoreElementMatcher(const Matcher<ElementAccess>& access_matcher,
                        const Matcher<Node*>& base_matcher,
                        const Matcher<Node*>& index_matcher,
                        const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher,
                        const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kStoreElement),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(ElementAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<ElementAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

#define LOAD_MATCHER(kLoad)                                                   \
  class Is##kLoad##Matcher final : public TestNodeMatcher {                   \
   public:                                                                    \
    Is##kLoad##Matcher(const Matcher<LoadRepresentation>& rep_matcher,        \
                       const Matcher<Node*>& base_matcher,                    \
                       const Matcher<Node*>& index_matcher,                   \
                       const Matcher<Node*>& effect_matcher,                  \
                       const Matcher<Node*>& control_matcher)                 \
        : TestNodeMatcher(IrOpcode::k##kLoad),                                \
          rep_matcher_(rep_matcher),                                          \
          base_matcher_(base_matcher),                                        \
          index_matcher_(index_matcher),                                      \
          effect_matcher_(effect_matcher),                                    \
          control_matcher_(control_matcher) {}                                \
                                                                              \
    void DescribeTo(std::ostream* os) const final {                           \
      TestNodeMatcher::DescribeTo(os);                                        \
      *os << " whose rep (";                                                  \
      rep_matcher_.DescribeTo(os);                                            \
      *os << "), base (";                                                     \
      base_matcher_.DescribeTo(os);                                           \
      *os << "), index (";                                                    \
      index_matcher_.DescribeTo(os);                                          \
      *os << "), effect (";                                                   \
      effect_matcher_.DescribeTo(os);                                         \
      *os << ") and control (";                                               \
      control_matcher_.DescribeTo(os);                                        \
      *os << ")";                                                             \
    }                                                                         \
                                                                              \
    bool MatchAndExplain(Node* node,                                          \
                         MatchResultListener* listener) const final {         \
      Node* effect_node = nullptr;                                            \
      Node* control_node = nullptr;                                           \
      if (NodeProperties::FirstEffectIndex(node) < node->InputCount()) {      \
        effect_node = NodeProperties::GetEffectInput(node);                   \
      }                                                                       \
      if (NodeProperties::FirstControlIndex(node) < node->InputCount()) {     \
        control_node = NodeProperties::GetControlInput(node);                 \
      }                                                                       \
      LoadRepresentation rep = IrOpcode::kLoadFromObject == node->opcode()    \
                                   ? ObjectAccessOf(node->op()).machine_type  \
                                   : LoadRepresentationOf(node->op());        \
      return (TestNodeMatcher::MatchAndExplain(node, listener) &&             \
              PrintMatchAndExplain(rep, "rep", rep_matcher_, listener) &&     \
              PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),    \
                                   "base", base_matcher_, listener) &&        \
              PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),    \
                                   "index", index_matcher_, listener) &&      \
              PrintMatchAndExplain(effect_node, "effect", effect_matcher_,    \
                                   listener) &&                               \
              PrintMatchAndExplain(control_node, "control", control_matcher_, \
                                   listener));                                \
    }                                                                         \
                                                                              \
   private:                                                                   \
    const Matcher<LoadRepresentation> rep_matcher_;                           \
    const Matcher<Node*> base_matcher_;                                       \
    const Matcher<Node*> index_matcher_;                                      \
    const Matcher<Node*> effect_matcher_;                                     \
    const Matcher<Node*> control_matcher_;                                    \
  };

LOAD_MATCHER(Load)
LOAD_MATCHER(UnalignedLoad)
LOAD_MATCHER(LoadFromObject)

class IsLoadImmutableMatcher final : public TestNodeMatcher {
 public:
  IsLoadImmutableMatcher(const Matcher<LoadRepresentation>& rep_matcher,
                         const Matcher<Node*>& base_matcher,
                         const Matcher<Node*>& index_matcher)
      : TestNodeMatcher(IrOpcode::kLoadImmutable),
        rep_matcher_(rep_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose rep (";
    rep_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << ") and index (";
    index_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    LoadRepresentation rep = LoadRepresentationOf(node->op());
    return TestNodeMatcher::MatchAndExplain(node, listener) &&
           PrintMatchAndExplain(rep, "rep", rep_matcher_, listener) &&
           PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                base_matcher_, listener) &&
           PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "index",
                                index_matcher_, listener);
  }

 private:
  const Matcher<LoadRepresentation> rep_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
};

#define STORE_MATCHER(kStore, representation)                                 \
  class Is##kStore##Matcher final : public TestNodeMatcher {                  \
   public:                                                                    \
    Is##kStore##Matcher(const Matcher<representation>& rep_matcher,           \
                        const Matcher<Node*>& base_matcher,                   \
                        const Matcher<Node*>& index_matcher,                  \
                        const Matcher<Node*>& value_matcher,                  \
                        const Matcher<Node*>& effect_matcher,                 \
                        const Matcher<Node*>& control_matcher)                \
        : TestNodeMatcher(IrOpcode::k##kStore),                               \
          rep_matcher_(rep_matcher),                                          \
          base_matcher_(base_matcher),                                        \
          index_matcher_(index_matcher),                                      \
          value_matcher_(value_matcher),                                      \
          effect_matcher_(effect_matcher),                                    \
          control_matcher_(control_matcher) {}                                \
                                                                              \
    void DescribeTo(std::ostream* os) const final {                           \
      TestNodeMatcher::DescribeTo(os);                                        \
      *os << " whose rep (";                                                  \
      rep_matcher_.DescribeTo(os);                                            \
      *os << "), base (";                                                     \
      base_matcher_.DescribeTo(os);                                           \
      *os << "), index (";                                                    \
      index_matcher_.DescribeTo(os);                                          \
      *os << "), value (";                                                    \
      value_matcher_.DescribeTo(os);                                          \
      *os << "), effect (";                                                   \
      effect_matcher_.DescribeTo(os);                                         \
      *os << ") and control (";                                               \
      control_matcher_.DescribeTo(os);                                        \
      *os << ")";                                                             \
    }                                                                         \
                                                                              \
    bool MatchAndExplain(Node* node,                                          \
                         MatchResultListener* listener) const final {         \
      Node* effect_node = nullptr;                                            \
      Node* control_node = nullptr;                                           \
      if (NodeProperties::FirstEffectIndex(node) < node->InputCount()) {      \
        effect_node = NodeProperties::GetEffectInput(node);                   \
      }                                                                       \
      if (NodeProperties::FirstControlIndex(node) < node->InputCount()) {     \
        control_node = NodeProperties::GetControlInput(node);                 \
      }                                                                       \
      return (TestNodeMatcher::MatchAndExplain(node, listener) &&             \
              PrintMatchAndExplain(OpParameter<representation>(node->op()),   \
                                   "rep", rep_matcher_, listener) &&          \
              PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),    \
                                   "base", base_matcher_, listener) &&        \
              PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),    \
                                   "index", index_matcher_, listener) &&      \
              PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),    \
                                   "value", value_matcher_, listener) &&      \
              PrintMatchAndExplain(effect_node, "effect", effect_matcher_,    \
                                   listener) &&                               \
              PrintMatchAndExplain(control_node, "control", control_matcher_, \
                                   listener));                                \
    }                                                                         \
                                                                              \
   private:                                                                   \
    const Matcher<representation> rep_matcher_;                               \
    const Matcher<Node*> base_matcher_;                                       \
    const Matcher<Node*> index_matcher_;                                      \
    const Matcher<Node*> value_matcher_;                                      \
    const Matcher<Node*> effect_matcher_;                                     \
    const Matcher<Node*> control_matcher_;                                    \
  };

STORE_MATCHER(Store, StoreRepresentation)
STORE_MATCHER(UnalignedStore, UnalignedStoreRepresentation)
STORE_MATCHER(StoreToObject, ObjectAccess)

class IsStackSlotMatcher final : public TestNodeMatcher {
 public:
  explicit IsStackSlotMatcher(
      const Matcher<StackSlotRepresentation>& rep_matcher)
      : TestNodeMatcher(IrOpcode::kStackSlot), rep_matcher_(rep_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose rep (";
    rep_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(StackSlotRepresentationOf(node->op()), "rep",
                                 rep_matcher_, listener));
  }

 private:
  const Matcher<StackSlotRepresentation> rep_matcher_;
};

class IsToNumberMatcher final : public TestNodeMatcher {
 public:
  IsToNumberMatcher(const Matcher<Node*>& base_matcher,
                    const Matcher<Node*>& context_matcher,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : TestNodeMatcher(IrOpcode::kJSToNumber),
        base_matcher_(base_matcher),
        context_matcher_(context_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose base (";
    base_matcher_.DescribeTo(os);
    *os << "), context (";
    context_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetContextInput(node),
                                 "context", context_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> context_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};

class IsLoadContextMatcher final : public TestNodeMatcher {
 public:
  IsLoadContextMatcher(const Matcher<ContextAccess>& access_matcher,
                       const Matcher<Node*>& context_matcher)
      : TestNodeMatcher(IrOpcode::kJSLoadContext),
        access_matcher_(access_matcher),
        context_matcher_(context_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << ") and context (";
    context_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(ContextAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetContextInput(node),
                                 "context", context_matcher_, listener));
  }

 private:
  const Matcher<ContextAccess> access_matcher_;
  const Matcher<Node*> context_matcher_;
};

class IsQuadopMatcher final : public TestNodeMatcher {
 public:
  IsQuadopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& a_matcher,
                  const Matcher<Node*>& b_matcher,
                  const Matcher<Node*>& c_matcher,
                  const Matcher<Node*>& d_matcher)
      : TestNodeMatcher(opcode),
        a_matcher_(a_matcher),
        b_matcher_(b_matcher),
        c_matcher_(c_matcher),
        d_matcher_(d_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose a (";
    a_matcher_.DescribeTo(os);
    *os << ") and b (";
    b_matcher_.DescribeTo(os);
    *os << ") and c (";
    c_matcher_.DescribeTo(os);
    *os << ") and d (";
    d_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "a",
                                 a_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "b",
                                 b_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2), "c",
                                 c_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 3), "d",
                                 d_matcher_, listener));
  }

 private:
  const Matcher<Node*> a_matcher_;
  const Matcher<Node*> b_matcher_;
  const Matcher<Node*> c_matcher_;
  const Matcher<Node*> d_matcher_;
};

class IsTernopMatcher final : public TestNodeMatcher {
 public:
  IsTernopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& lhs_matcher,
                  const Matcher<Node*>& mid_matcher,
                  const Matcher<Node*>& rhs_matcher)
      : TestNodeMatcher(opcode),
        lhs_matcher_(lhs_matcher),
        mid_matcher_(mid_matcher),
        rhs_matcher_(rhs_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose lhs (";
    lhs_matcher_.DescribeTo(os);
    *os << ") and mid (";
    mid_matcher_.DescribeTo(os);
    *os << ") and rhs (";
    rhs_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "mid",
                                 mid_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2), "rhs",
                                 rhs_matcher_, listener));
  }

 private:
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> mid_matcher_;
  const Matcher<Node*> rhs_matcher_;
};

class IsBinopMatcher final : public TestNodeMatcher {
 public:
  IsBinopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& lhs_matcher,
                 const Matcher<Node*>& rhs_matcher)
      : TestNodeMatcher(opcode),
        lhs_matcher_(lhs_matcher),
        rhs_matcher_(rhs_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose lhs (";
    lhs_matcher_.DescribeTo(os);
    *os << ") and rhs (";
    rhs_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "rhs",
                                 rhs_matcher_, listener));
  }

 private:
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> rhs_matcher_;
};

class IsStringConcatMatcher final : public TestNodeMatcher {
 public:
  IsStringConcatMatcher(const Matcher<Node*>& length_matcher,
                        const Matcher<Node*>& lhs_matcher,
                        const Matcher<Node*>& rhs_matcher)
      : TestNodeMatcher(IrOpcode::kStringConcat),
        length_matcher_(length_matcher),
        lhs_matcher_(lhs_matcher),
        rhs_matcher_(rhs_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose length (";
    length_matcher_.DescribeTo(os);
    *os << ") and lhs (";
    lhs_matcher_.DescribeTo(os);
    *os << ") and rhs (";
    rhs_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "length", length_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2), "rhs",
                                 rhs_matcher_, listener));
  }

 private:
  const Matcher<Node*> length_matcher_;
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> rhs_matcher_;
};

class IsUnopMatcher final : public TestNodeMatcher {
 public:
  IsUnopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& input_matcher)
      : TestNodeMatcher(opcode), input_matcher_(input_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    TestNodeMatcher::DescribeTo(os);
    *os << " whose input (";
    input_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "input", input_matcher_, listener));
  }

 private:
  const Matcher<Node*> input_matcher_;
};

class IsParameterMatcher final : public TestNodeMatcher {
 public:
  explicit IsParameterMatcher(const Matcher<int>& index_matcher)
      : TestNodeMatcher(IrOpcode::kParameter), index_matcher_(index_matcher) {}

  void DescribeTo(std::ostream* os) const override {
    *os << "is a Parameter node with index(";
    index_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (TestNodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(ParameterIndexOf(node->op()), "index",
                                 index_matcher_, listener));
  }

 private:
  const Matcher<int> index_matcher_;
};

}  // namespace

Matcher<Node*> IsDead() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kDead));
}

Matcher<Node*> IsUnreachable() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kUnreachable));
}

Matcher<Node*> IsThrow() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kThrow));
}

Matcher<Node*> IsStart() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kStart));
}

Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher) {
  return MakeMatcher(new IsControl1Matcher(IrOpcode::kEnd, control0_matcher));
}


Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher,
                     const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kEnd, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher,
                     const Matcher<Node*>& control1_matcher,
                     const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kEnd, control0_matcher,
                                           control1_matcher, control2_matcher));
}


Matcher<Node*> IsBranch(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsBranchMatcher(value_matcher, control_matcher));
}


Matcher<Node*> IsMerge(const Matcher<Node*>& control0_matcher,
                       const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kMerge, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsMerge(const Matcher<Node*>& control0_matcher,
                       const Matcher<Node*>& control1_matcher,
                       const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kMerge, control0_matcher,
                                           control1_matcher, control2_matcher));
}


Matcher<Node*> IsLoop(const Matcher<Node*>& control0_matcher,
                      const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kLoop, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsLoop(const Matcher<Node*>& control0_matcher,
                      const Matcher<Node*>& control1_matcher,
                      const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kLoop, control0_matcher,
                                           control1_matcher, control2_matcher));
}

Matcher<Node*> IsLoopExitValue(const Matcher<MachineRepresentation> rep_matcher,
                               const Matcher<Node*>& value_matcher) {
  return MakeMatcher(new IsLoopExitValueMatcher(rep_matcher, value_matcher));
}

Matcher<Node*> IsIfTrue(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsControl1Matcher(IrOpcode::kIfTrue, control_matcher));
}


Matcher<Node*> IsIfFalse(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfFalse, control_matcher));
}


Matcher<Node*> IsIfSuccess(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfSuccess, control_matcher));
}


Matcher<Node*> IsSwitch(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsSwitchMatcher(value_matcher, control_matcher));
}

Matcher<Node*> IsIfValue(const Matcher<IfValueParameters>& value_matcher,
                         const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsIfValueMatcher(value_matcher, control_matcher));
}


Matcher<Node*> IsIfDefault(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfDefault, control_matcher));
}


Matcher<Node*> IsBeginRegion(const Matcher<Node*>& effect_matcher) {
  return MakeMatcher(new IsBeginRegionMatcher(effect_matcher));
}


Matcher<Node*> IsFinishRegion(const Matcher<Node*>& value_matcher,
                              const Matcher<Node*>& effect_matcher) {
  return MakeMatcher(new IsFinishRegionMatcher(value_matcher, effect_matcher));
}


Matcher<Node*> IsReturn(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsReturnMatcher(value_matcher, effect_matcher, control_matcher));
}

Matcher<Node*> IsReturn2(const Matcher<Node*>& value_matcher,
                         const Matcher<Node*>& value2_matcher,
                         const Matcher<Node*>& effect_matcher,
                         const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsReturnMatcher(value_matcher, value2_matcher,
                                         effect_matcher, control_matcher));
}

Matcher<Node*> IsTerminate(const Matcher<Node*>& effect_matcher,
                           const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsTerminateMatcher(effect_matcher, control_matcher));
}

Matcher<Node*> IsTypeGuard(const Matcher<Node*>& value_matcher,
                           const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsTypeGuardMatcher(value_matcher, control_matcher));
}

Matcher<Node*> IsExternalConstant(
    const Matcher<ExternalReference>& value_matcher) {
  return MakeMatcher(new IsConstantMatcher<ExternalReference>(
      IrOpcode::kExternalConstant, value_matcher));
}


Matcher<Node*> IsHeapConstant(Handle<HeapObject> value) {
  return MakeMatcher(new IsConstantMatcher<Handle<HeapObject>>(
      IrOpcode::kHeapConstant, value));
}


Matcher<Node*> IsInt32Constant(const Matcher<int32_t>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<int32_t>(IrOpcode::kInt32Constant, value_matcher));
}


Matcher<Node*> IsInt64Constant(const Matcher<int64_t>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<int64_t>(IrOpcode::kInt64Constant, value_matcher));
}


Matcher<Node*> IsFloat32Constant(const Matcher<float>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<float>(IrOpcode::kFloat32Constant, value_matcher));
}


Matcher<Node*> IsFloat64Constant(const Matcher<double>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<double>(IrOpcode::kFloat64Constant, value_matcher));
}


Matcher<Node*> IsNumberConstant(const Matcher<double>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<double>(IrOpcode::kNumberConstant, value_matcher));
}

Matcher<Node*> IsPointerConstant(const Matcher<intptr_t>& value_matcher) {
  return MakeMatcher(new IsConstantMatcher<intptr_t>(IrOpcode::kPointerConstant,
                                                     value_matcher));
}

Matcher<Node*> IsSelect(const Matcher<MachineRepresentation>& type_matcher,
                        const Matcher<Node*>& value0_matcher,
                        const Matcher<Node*>& value1_matcher,
                        const Matcher<Node*>& value2_matcher) {
  return MakeMatcher(new IsSelectMatcher(type_matcher, value0_matcher,
                                         value1_matcher, value2_matcher));
}


Matcher<Node*> IsPhi(const Matcher<MachineRepresentation>& type_matcher,
                     const Matcher<Node*>& value0_matcher,
                     const Matcher<Node*>& value1_matcher,
                     const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(new IsPhiMatcher(type_matcher, value0_matcher,
                                      value1_matcher, merge_matcher));
}


Matcher<Node*> IsPhi(const Matcher<MachineRepresentation>& type_matcher,
                     const Matcher<Node*>& value0_matcher,
                     const Matcher<Node*>& value1_matcher,
                     const Matcher<Node*>& value2_matcher,
                     const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(new IsPhi2Matcher(type_matcher, value0_matcher,
                                       value1_matcher, value2_matcher,
                                       merge_matcher));
}


Matcher<Node*> IsEffectPhi(const Matcher<Node*>& effect0_matcher,
                           const Matcher<Node*>& effect1_matcher,
                           const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(
      new IsEffectPhiMatcher(effect0_matcher, effect1_matcher, merge_matcher));
}


Matcher<Node*> IsProjection(const Matcher<size_t>& index_matcher,
                            const Matcher<Node*>& base_matcher) {
  return MakeMatcher(new IsProjectionMatcher(index_matcher, base_matcher));
}

Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}

Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& value4_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<const CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& value4_matcher,
                      const Matcher<Node*>& value5_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(
    const Matcher<const CallDescriptor*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& value5_matcher,
    const Matcher<Node*>& value6_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  value_matchers.push_back(value6_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& value5_matcher,
    const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& value5_matcher,
    const Matcher<Node*>& value6_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  value_matchers.push_back(value6_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& value5_matcher,
    const Matcher<Node*>& value6_matcher, const Matcher<Node*>& value7_matcher,
    const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  value_matchers.push_back(value6_matcher);
  value_matchers.push_back(value7_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}

#define DEFINE_SPECULATIVE_BINOP_MATCHER(opcode)                              \
  Matcher<Node*> Is##opcode(const Matcher<NumberOperationHint>& hint_matcher, \
                            const Matcher<Node*>& lhs_matcher,                \
                            const Matcher<Node*>& rhs_matcher,                \
                            const Matcher<Node*>& effect_matcher,             \
                            const Matcher<Node*>& control_matcher) {          \
    return MakeMatcher(new IsSpeculativeBinopMatcher(                         \
        IrOpcode::k##opcode, hint_matcher, lhs_matcher, rhs_matcher,          \
        effect_matcher, control_matcher));                                    \
  }
SIMPLIFIED_SPECULATIVE_NUMBER_BINOP_LIST(DEFINE_SPECULATIVE_BINOP_MATCHER)
DEFINE_SPECULATIVE_BINOP_MATCHER(SpeculativeNumberEqual)
DEFINE_SPECULATIVE_BINOP_MATCHER(SpeculativeNumberLessThan)
DEFINE_SPECULATIVE_BINOP_MATCHER(SpeculativeNumberLessThanOrEqual)
#undef DEFINE_SPECULATIVE_BINOP_MATCHER

Matcher<Node*> IsStringConcat(const Matcher<Node*>& length_matcher,
                              const Matcher<Node*>& lhs_matcher,
                              const Matcher<Node*>& rhs_matcher) {
  return MakeMatcher(
      new IsStringConcatMatcher(length_matcher, lhs_matcher, rhs_matcher));
}

Matcher<Node*> IsAllocate(const Matcher<Node*>& size_matcher,
                          const Matcher<Node*>& effect_matcher,
                          const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsAllocateMatcher(size_matcher, effect_matcher, control_matcher));
}


Matcher<Node*> IsLoadField(const Matcher<FieldAccess>& access_matcher,
                           const Matcher<Node*>& base_matcher,
                           const Matcher<Node*>& effect_matcher,
                           const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadFieldMatcher(access_matcher, base_matcher,
                                            effect_matcher, control_matcher));
}


Matcher<Node*> IsStoreField(const Matcher<FieldAccess>& access_matcher,
                            const Matcher<Node*>& base_matcher,
                            const Matcher<Node*>& value_matcher,
                            const Matcher<Node*>& effect_matcher,
                            const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreFieldMatcher(access_matcher, base_matcher,
                                             value_matcher, effect_matcher,
                                             control_matcher));
}

Matcher<Node*> IsLoadElement(const Matcher<ElementAccess>& access_matcher,
                             const Matcher<Node*>& base_matcher,
                             const Matcher<Node*>& index_matcher,
                             const Matcher<Node*>& effect_matcher,
                             const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadElementMatcher(access_matcher, base_matcher,
                                              index_matcher, effect_matcher,
                                              control_matcher));
}


Matcher<Node*> IsStoreElement(const Matcher<ElementAccess>& access_matcher,
                              const Matcher<Node*>& base_matcher,
                              const Matcher<Node*>& index_matcher,
                              const Matcher<Node*>& value_matcher,
                              const Matcher<Node*>& effect_matcher,
                              const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreElementMatcher(
      access_matcher, base_matcher, index_matcher, value_matcher,
      effect_matcher, control_matcher));
}

Matcher<Node*> IsLoad(const Matcher<LoadRepresentation>& rep_matcher,
                      const Matcher<Node*>& base_matcher,
                      const Matcher<Node*>& index_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadMatcher(rep_matcher, base_matcher, index_matcher,
                                       effect_matcher, control_matcher));
}

Matcher<Node*> IsUnalignedLoad(const Matcher<LoadRepresentation>& rep_matcher,
                               const Matcher<Node*>& base_matcher,
                               const Matcher<Node*>& index_matcher,
                               const Matcher<Node*>& effect_matcher,
                               const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsUnalignedLoadMatcher(rep_matcher, base_matcher,
                                                index_matcher, effect_matcher,
                                                control_matcher));
}

Matcher<Node*> IsLoadFromObject(const Matcher<LoadRepresentation>& rep_matcher,
                                const Matcher<Node*>& base_matcher,
                                const Matcher<Node*>& index_matcher,
                                const Matcher<Node*>& effect_matcher,
                                const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadFromObjectMatcher(rep_matcher, base_matcher,
                                                 index_matcher, effect_matcher,
                                                 control_matcher));
}

Matcher<Node*> IsLoadImmutable(const Matcher<LoadRepresentation>& rep_matcher,
                               const Matcher<Node*>& base_matcher,
                               const Matcher<Node*>& index_matcher) {
  return MakeMatcher(
      new IsLoadImmutableMatcher(rep_matcher, base_matcher, index_matcher));
}

Matcher<Node*> IsStore(const Matcher<StoreRepresentation>& rep_matcher,
                       const Matcher<Node*>& base_matcher,
                       const Matcher<Node*>& index_matcher,
                       const Matcher<Node*>& value_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreMatcher(rep_matcher, base_matcher,
                                        index_matcher, value_matcher,
                                        effect_matcher, control_matcher));
}

Matcher<Node*> IsUnalignedStore(
    const Matcher<UnalignedStoreRepresentation>& rep_matcher,
    const Matcher<Node*>& base_matcher, const Matcher<Node*>& index_matcher,
    const Matcher<Node*>& value_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsUnalignedStoreMatcher(
      rep_matcher, base_matcher, index_matcher, value_matcher, effect_matcher,
      control_matcher));
}

Matcher<Node*> IsStoreToObject(const Matcher<ObjectAccess>& rep_matcher,
                               const Matcher<Node*>& base_matcher,
                               const Matcher<Node*>& index_matcher,
                               const Matcher<Node*>& value_matcher,
                               const Matcher<Node*>& effect_matcher,
                               const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreToObjectMatcher(
      rep_matcher, base_matcher, index_matcher, value_matcher, effect_matcher,
      control_matcher));
}

Matcher<Node*> IsStackSlot(
    const Matcher<StackSlotRepresentation>& rep_matcher) {
  return MakeMatcher(new IsStackSlotMatcher(rep_matcher));
}

Matcher<Node*> IsToNumber(const Matcher<Node*>& base_matcher,
                          const Matcher<Node*>& context_matcher,
                          const Matcher<Node*>& effect_matcher,
                          const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsToNumberMatcher(base_matcher, context_matcher,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsLoadContext(const Matcher<ContextAccess>& access_matcher,
                             const Matcher<Node*>& context_matcher) {
  return MakeMatcher(new IsLoadContextMatcher(access_matcher, context_matcher));
}


Matcher<Node*> IsParameter(const Matcher<int> index_matcher) {
  return MakeMatcher(new IsParameterMatcher(index_matcher));
}

Matcher<Node*> IsLoadFramePointer() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kLoadFramePointer));
}

Matcher<Node*> IsLoadParentFramePointer() {
  return MakeMatcher(new TestNodeMatcher(IrOpcode::kLoadParentFramePointer));
}

#define IS_QUADOP_MATCHER(Name)                                               \
  Matcher<Node*> Is##Name(                                                    \
      const Matcher<Node*>& a_matcher, const Matcher<Node*>& b_matcher,       \
      const Matcher<Node*>& c_matcher, const Matcher<Node*>& d_matcher) {     \
    return MakeMatcher(new IsQuadopMatcher(IrOpcode::k##Name, a_matcher,      \
                                           b_matcher, c_matcher, d_matcher)); \
  }

IS_QUADOP_MATCHER(Int32PairAdd)
IS_QUADOP_MATCHER(Int32PairSub)
IS_QUADOP_MATCHER(Int32PairMul)

#define IS_TERNOP_MATCHER(Name)                                            \
  Matcher<Node*> Is##Name(const Matcher<Node*>& lhs_matcher,               \
                          const Matcher<Node*>& mid_matcher,               \
                          const Matcher<Node*>& rhs_matcher) {             \
    return MakeMatcher(new IsTernopMatcher(IrOpcode::k##Name, lhs_matcher, \
                                           mid_matcher, rhs_matcher));     \
  }

IS_TERNOP_MATCHER(Word32PairShl)
IS_TERNOP_MATCHER(Word32PairShr)
IS_TERNOP_MATCHER(Word32PairSar)

#define IS_BINOP_MATCHER(Name)                                            \
  Matcher<Node*> Is##Name(const Matcher<Node*>& lhs_matcher,              \
                          const Matcher<Node*>& rhs_matcher) {            \
    return MakeMatcher(                                                   \
        new IsBinopMatcher(IrOpcode::k##Name, lhs_matcher, rhs_matcher)); \
  }
IS_BINOP_MATCHER(NumberEqual)
IS_BINOP_MATCHER(NumberLessThan)
IS_BINOP_MATCHER(NumberSubtract)
IS_BINOP_MATCHER(NumberMultiply)
IS_BINOP_MATCHER(NumberShiftLeft)
IS_BINOP_MATCHER(NumberShiftRight)
IS_BINOP_MATCHER(NumberShiftRightLogical)
IS_BINOP_MATCHER(NumberImul)
IS_BINOP_MATCHER(NumberAtan2)
IS_BINOP_MATCHER(NumberMax)
IS_BINOP_MATCHER(NumberMin)
IS_BINOP_MATCHER(NumberPow)
IS_BINOP_MATCHER(ReferenceEqual)
IS_BINOP_MATCHER(Word32And)
IS_BINOP_MATCHER(Word32Or)
IS_BINOP_MATCHER(Word32Xor)
IS_BINOP_MATCHER(Word32Sar)
IS_BINOP_MATCHER(Word32Shl)
IS_BINOP_MATCHER(Word32Shr)
IS_BINOP_MATCHER(Word32Ror)
IS_BINOP_MATCHER(Word32Equal)
IS_BINOP_MATCHER(Word64And)
IS_BINOP_MATCHER(Word64Or)
IS_BINOP_MATCHER(Word64Xor)
IS_BINOP_MATCHER(Word64Sar)
IS_BINOP_MATCHER(Word64Shl)
IS_BINOP_MATCHER(Word64Shr)
IS_BINOP_MATCHER(Word64Equal)
IS_BINOP_MATCHER(Int32AddWithOverflow)
IS_BINOP_MATCHER(Int32SubWithOverflow)
IS_BINOP_MATCHER(Int32Add)
IS_BINOP_MATCHER(Int32Div)
IS_BINOP_MATCHER(Int32Sub)
IS_BINOP_MATCHER(Int32Mul)
IS_BINOP_MATCHER(Int32MulHigh)
IS_BINOP_MATCHER(Int32LessThan)
IS_BINOP_MATCHER(Uint32LessThan)
IS_BINOP_MATCHER(Uint32LessThanOrEqual)
IS_BINOP_MATCHER(Int64Add)
IS_BINOP_MATCHER(Int64Div)
IS_BINOP_MATCHER(Int64Sub)
IS_BINOP_MATCHER(Int64Mul)
IS_BINOP_MATCHER(Int64MulHigh)
IS_BINOP_MATCHER(Int64LessThan)
IS_BINOP_MATCHER(Uint64LessThan)
IS_BINOP_MATCHER(JSAdd)
IS_BINOP_MATCHER(JSParseInt)
IS_BINOP_MATCHER(Float32Equal)
IS_BINOP_MATCHER(Float32LessThan)
IS_BINOP_MATCHER(Float32LessThanOrEqual)
IS_BINOP_MATCHER(Float64Max)
IS_BINOP_MATCHER(Float64Min)
IS_BINOP_MATCHER(Float64Add)
IS_BINOP_MATCHER(Float64Sub)
IS_BINOP_MATCHER(Float64Mul)
IS_BINOP_MATCHER(Float64InsertLowWord32)
IS_BINOP_MATCHER(Float64InsertHighWord32)
#undef IS_BINOP_MATCHER


#define IS_UNOP_MATCHER(Name)                                                \
  Matcher<Node*> Is##Name(const Matcher<Node*>& input_matcher) {             \
    return MakeMatcher(new IsUnopMatcher(IrOpcode::k##Name, input_matcher)); \
  }
IS_UNOP_MATCHER(BooleanNot)
IS_UNOP_MATCHER(BitcastWordToTagged)
IS_UNOP_MATCHER(TruncateFloat64ToWord32)
IS_UNOP_MATCHER(ChangeFloat64ToInt32)
IS_UNOP_MATCHER(ChangeFloat64ToUint32)
IS_UNOP_MATCHER(ChangeInt32ToFloat64)
IS_UNOP_MATCHER(ChangeInt32ToInt64)
IS_UNOP_MATCHER(ChangeUint32ToFloat64)
IS_UNOP_MATCHER(ChangeUint32ToUint64)
IS_UNOP_MATCHER(TruncateFloat64ToFloat32)
IS_UNOP_MATCHER(TruncateInt64ToInt32)
IS_UNOP_MATCHER(Float32Abs)
IS_UNOP_MATCHER(Float32Neg)
IS_UNOP_MATCHER(Float64Abs)
IS_UNOP_MATCHER(Float64Neg)
IS_UNOP_MATCHER(Float64Sqrt)
IS_UNOP_MATCHER(Float64RoundDown)
IS_UNOP_MATCHER(Float64RoundTruncate)
IS_UNOP_MATCHER(Float64RoundTiesAway)
IS_UNOP_MATCHER(Float64ExtractLowWord32)
IS_UNOP_MATCHER(Float64ExtractHighWord32)
IS_UNOP_MATCHER(NumberAbs)
IS_UNOP_MATCHER(NumberAcos)
IS_UNOP_MATCHER(NumberAcosh)
IS_UNOP_MATCHER(NumberAsin)
IS_UNOP_MATCHER(NumberAsinh)
IS_UNOP_MATCHER(NumberAtan)
IS_UNOP_MATCHER(NumberAtanh)
IS_UNOP_MATCHER(NumberCeil)
IS_UNOP_MATCHER(NumberClz32)
IS_UNOP_MATCHER(NumberCbrt)
IS_UNOP_MATCHER(NumberCos)
IS_UNOP_MATCHER(NumberCosh)
IS_UNOP_MATCHER(NumberExp)
IS_UNOP_MATCHER(NumberExpm1)
IS_UNOP_MATCHER(NumberFloor)
IS_UNOP_MATCHER(NumberFround)
IS_UNOP_MATCHER(NumberLog)
IS_UNOP_MATCHER(NumberLog1p)
IS_UNOP_MATCHER(NumberLog10)
IS_UNOP_MATCHER(NumberLog2)
IS_UNOP_MATCHER(NumberRound)
IS_UNOP_MATCHER(NumberSign)
IS_UNOP_MATCHER(NumberSin)
IS_UNOP_MATCHER(NumberSinh)
IS_UNOP_MATCHER(NumberSqrt)
IS_UNOP_MATCHER(NumberTan)
IS_UNOP_MATCHER(NumberTanh)
IS_UNOP_MATCHER(NumberTrunc)
IS_UNOP_MATCHER(NumberToBoolean)
IS_UNOP_MATCHER(NumberToInt32)
IS_UNOP_MATCHER(NumberToUint32)
IS_UNOP_MATCHER(PlainPrimitiveToNumber)
IS_UNOP_MATCHER(ObjectIsFiniteNumber)
IS_UNOP_MATCHER(NumberIsFinite)
IS_UNOP_MATCHER(ObjectIsInteger)
IS_UNOP_MATCHER(ObjectIsSafeInteger)
IS_UNOP_MATCHER(ObjectIsNaN)
IS_UNOP_MATCHER(NumberIsNaN)
IS_UNOP_MATCHER(ObjectIsReceiver)
IS_UNOP_MATCHER(ObjectIsSmi)
IS_UNOP_MATCHER(ObjectIsUndetectable)
IS_UNOP_MATCHER(StringFromSingleCharCode)
IS_UNOP_MATCHER(StringLength)
IS_UNOP_MATCHER(Word32Clz)
IS_UNOP_MATCHER(Word32Ctz)
IS_UNOP_MATCHER(Word32Popcnt)
IS_UNOP_MATCHER(Word32ReverseBytes)
IS_UNOP_MATCHER(SpeculativeToNumber)
#undef IS_UNOP_MATCHER

// Special-case Bitcast operators which are disabled when ENABLE_VERIFY_CSA is
// not enabled.
Matcher<Node*> IsBitcastTaggedToWord(const Matcher<Node*>& input_matcher) {
  return MakeMatcher(
      new IsUnopMatcher(IrOpcode::kBitcastTaggedToWord, input_matcher));
}

Matcher<Node*> IsBitcastWordToTaggedSigned(
    const Matcher<Node*>& input_matcher) {
  return MakeMatcher(
      new IsUnopMatcher(IrOpcode::kBitcastWordToTaggedSigned, input_matcher));
}

#undef LOAD_MATCHER
#undef STORE_MATCHER
#undef IS_QUADOP_MATCHER
#undef IS_TERNOP_MATCHER

}  // namespace compiler
}  // namespace internal
}  // namespace v8
