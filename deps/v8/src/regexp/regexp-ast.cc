// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-ast.h"

#include "src/utils/ostreams.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

#define MAKE_ACCEPT(Name)                                          \
  void* RegExp##Name::Accept(RegExpVisitor* visitor, void* data) { \
    return visitor->Visit##Name(this, data);                       \
  }
FOR_EACH_REG_EXP_TREE_TYPE(MAKE_ACCEPT)
#undef MAKE_ACCEPT

#define MAKE_TYPE_CASE(Name)                               \
  RegExp##Name* RegExpTree::As##Name() { return nullptr; } \
  bool RegExpTree::Is##Name() { return false; }
FOR_EACH_REG_EXP_TREE_TYPE(MAKE_TYPE_CASE)
#undef MAKE_TYPE_CASE

#define MAKE_TYPE_CASE(Name)                              \
  RegExp##Name* RegExp##Name::As##Name() { return this; } \
  bool RegExp##Name::Is##Name() { return true; }
FOR_EACH_REG_EXP_TREE_TYPE(MAKE_TYPE_CASE)
#undef MAKE_TYPE_CASE

namespace {

Interval ListCaptureRegisters(ZoneList<RegExpTree*>* children) {
  Interval result = Interval::Empty();
  for (int i = 0; i < children->length(); i++)
    result = result.Union(children->at(i)->CaptureRegisters());
  return result;
}

}  // namespace

Interval RegExpAlternative::CaptureRegisters() {
  return ListCaptureRegisters(nodes());
}


Interval RegExpDisjunction::CaptureRegisters() {
  return ListCaptureRegisters(alternatives());
}


Interval RegExpLookaround::CaptureRegisters() {
  return body()->CaptureRegisters();
}


Interval RegExpCapture::CaptureRegisters() {
  Interval self(StartRegister(index()), EndRegister(index()));
  return self.Union(body()->CaptureRegisters());
}


Interval RegExpQuantifier::CaptureRegisters() {
  return body()->CaptureRegisters();
}


bool RegExpAssertion::IsAnchoredAtStart() {
  return assertion_type() == RegExpAssertion::Type::START_OF_INPUT;
}


bool RegExpAssertion::IsAnchoredAtEnd() {
  return assertion_type() == RegExpAssertion::Type::END_OF_INPUT;
}


bool RegExpAlternative::IsAnchoredAtStart() {
  ZoneList<RegExpTree*>* nodes = this->nodes();
  for (int i = 0; i < nodes->length(); i++) {
    RegExpTree* node = nodes->at(i);
    if (node->IsAnchoredAtStart()) {
      return true;
    }
    if (node->max_match() > 0) {
      return false;
    }
  }
  return false;
}


bool RegExpAlternative::IsAnchoredAtEnd() {
  ZoneList<RegExpTree*>* nodes = this->nodes();
  for (int i = nodes->length() - 1; i >= 0; i--) {
    RegExpTree* node = nodes->at(i);
    if (node->IsAnchoredAtEnd()) {
      return true;
    }
    if (node->max_match() > 0) {
      return false;
    }
  }
  return false;
}


bool RegExpDisjunction::IsAnchoredAtStart() {
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  for (int i = 0; i < alternatives->length(); i++) {
    if (!alternatives->at(i)->IsAnchoredAtStart()) return false;
  }
  return true;
}


bool RegExpDisjunction::IsAnchoredAtEnd() {
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  for (int i = 0; i < alternatives->length(); i++) {
    if (!alternatives->at(i)->IsAnchoredAtEnd()) return false;
  }
  return true;
}


bool RegExpLookaround::IsAnchoredAtStart() {
  return is_positive() && type() == LOOKAHEAD && body()->IsAnchoredAtStart();
}


bool RegExpCapture::IsAnchoredAtStart() { return body()->IsAnchoredAtStart(); }


bool RegExpCapture::IsAnchoredAtEnd() { return body()->IsAnchoredAtEnd(); }

namespace {

// Convert regular expression trees to a simple sexp representation.
// This representation should be different from the input grammar
// in as many cases as possible, to make it more difficult for incorrect
// parses to look as correct ones which is likely if the input and
// output formats are alike.
class RegExpUnparser final : public RegExpVisitor {
 public:
  RegExpUnparser(std::ostream& os, Zone* zone) : os_(os), zone_(zone) {}
  void VisitCharacterRange(CharacterRange that);
#define MAKE_CASE(Name) void* Visit##Name(RegExp##Name*, void* data) override;
  FOR_EACH_REG_EXP_TREE_TYPE(MAKE_CASE)
#undef MAKE_CASE
 private:
  std::ostream& os_;
  Zone* zone_;
};

}  // namespace

void* RegExpUnparser::VisitDisjunction(RegExpDisjunction* that, void* data) {
  os_ << "(|";
  for (int i = 0; i < that->alternatives()->length(); i++) {
    os_ << " ";
    that->alternatives()->at(i)->Accept(this, data);
  }
  os_ << ")";
  return nullptr;
}


void* RegExpUnparser::VisitAlternative(RegExpAlternative* that, void* data) {
  os_ << "(:";
  for (int i = 0; i < that->nodes()->length(); i++) {
    os_ << " ";
    that->nodes()->at(i)->Accept(this, data);
  }
  os_ << ")";
  return nullptr;
}


void RegExpUnparser::VisitCharacterRange(CharacterRange that) {
  os_ << AsUC32(that.from());
  if (!that.IsSingleton()) {
    os_ << "-" << AsUC32(that.to());
  }
}

void* RegExpUnparser::VisitClassRanges(RegExpClassRanges* that, void* data) {
  if (that->is_negated()) os_ << "^";
  os_ << "[";
  for (int i = 0; i < that->ranges(zone_)->length(); i++) {
    if (i > 0) os_ << " ";
    VisitCharacterRange(that->ranges(zone_)->at(i));
  }
  os_ << "]";
  return nullptr;
}

void* RegExpUnparser::VisitClassSetOperand(RegExpClassSetOperand* that,
                                           void* data) {
  os_ << "![";
  for (int i = 0; i < that->ranges()->length(); i++) {
    if (i > 0) os_ << " ";
    VisitCharacterRange(that->ranges()->at(i));
  }
  if (that->has_strings()) {
    for (auto iter : *that->strings()) {
      os_ << " '";
      os_ << std::string(iter.first.begin(), iter.first.end());
      os_ << "'";
    }
  }
  os_ << "]";
  return nullptr;
}

void* RegExpUnparser::VisitClassSetExpression(RegExpClassSetExpression* that,
                                              void* data) {
  switch (that->operation()) {
    case RegExpClassSetExpression::OperationType::kUnion:
      os_ << "++";
      break;
    case RegExpClassSetExpression::OperationType::kIntersection:
      os_ << "&&";
      break;
    case RegExpClassSetExpression::OperationType::kSubtraction:
      os_ << "--";
      break;
  }
  if (that->is_negated()) os_ << "^";
  os_ << "[";
  for (int i = 0; i < that->operands()->length(); i++) {
    if (i > 0) os_ << " ";
    that->operands()->at(i)->Accept(this, data);
  }
  os_ << "]";
  return nullptr;
}

void* RegExpUnparser::VisitAssertion(RegExpAssertion* that, void* data) {
  switch (that->assertion_type()) {
    case RegExpAssertion::Type::START_OF_INPUT:
      os_ << "@^i";
      break;
    case RegExpAssertion::Type::END_OF_INPUT:
      os_ << "@$i";
      break;
    case RegExpAssertion::Type::START_OF_LINE:
      os_ << "@^l";
      break;
    case RegExpAssertion::Type::END_OF_LINE:
      os_ << "@$l";
      break;
    case RegExpAssertion::Type::BOUNDARY:
      os_ << "@b";
      break;
    case RegExpAssertion::Type::NON_BOUNDARY:
      os_ << "@B";
      break;
  }
  return nullptr;
}


void* RegExpUnparser::VisitAtom(RegExpAtom* that, void* data) {
  os_ << "'";
  base::Vector<const base::uc16> chardata = that->data();
  for (int i = 0; i < chardata.length(); i++) {
    os_ << AsUC16(chardata[i]);
  }
  os_ << "'";
  return nullptr;
}


void* RegExpUnparser::VisitText(RegExpText* that, void* data) {
  if (that->elements()->length() == 1) {
    that->elements()->at(0).tree()->Accept(this, data);
  } else {
    os_ << "(!";
    for (int i = 0; i < that->elements()->length(); i++) {
      os_ << " ";
      that->elements()->at(i).tree()->Accept(this, data);
    }
    os_ << ")";
  }
  return nullptr;
}


void* RegExpUnparser::VisitQuantifier(RegExpQuantifier* that, void* data) {
  os_ << "(# " << that->min() << " ";
  if (that->max() == RegExpTree::kInfinity) {
    os_ << "- ";
  } else {
    os_ << that->max() << " ";
  }
  os_ << (that->is_greedy() ? "g " : that->is_possessive() ? "p " : "n ");
  that->body()->Accept(this, data);
  os_ << ")";
  return nullptr;
}


void* RegExpUnparser::VisitCapture(RegExpCapture* that, void* data) {
  os_ << "(^ ";
  that->body()->Accept(this, data);
  os_ << ")";
  return nullptr;
}

void* RegExpUnparser::VisitGroup(RegExpGroup* that, void* data) {
  os_ << "(?" << that->flags() << ": ";
  that->body()->Accept(this, data);
  os_ << ")";
  return nullptr;
}

void* RegExpUnparser::VisitLookaround(RegExpLookaround* that, void* data) {
  os_ << "(";
  os_ << (that->type() == RegExpLookaround::LOOKAHEAD ? "->" : "<-");
  os_ << (that->is_positive() ? " + " : " - ");
  that->body()->Accept(this, data);
  os_ << ")";
  return nullptr;
}


void* RegExpUnparser::VisitBackReference(RegExpBackReference* that,
                                         void* data) {
  os_ << "(<- " << that->captures()->first()->index();
  for (int i = 1; i < that->captures()->length(); ++i) {
    os_ << "," << that->captures()->at(i)->index();
  }
  os_ << ")";
  return nullptr;
}


void* RegExpUnparser::VisitEmpty(RegExpEmpty* that, void* data) {
  os_ << '%';
  return nullptr;
}

std::ostream& RegExpTree::Print(std::ostream& os, Zone* zone) {
  RegExpUnparser unparser(os, zone);
  Accept(&unparser, nullptr);
  return os;
}

RegExpDisjunction::RegExpDisjunction(ZoneList<RegExpTree*>* alternatives)
    : alternatives_(alternatives) {
  DCHECK_LT(1, alternatives->length());
  RegExpTree* first_alternative = alternatives->at(0);
  min_match_ = first_alternative->min_match();
  max_match_ = first_alternative->max_match();
  for (int i = 1; i < alternatives->length(); i++) {
    RegExpTree* alternative = alternatives->at(i);
    min_match_ = std::min(min_match_, alternative->min_match());
    max_match_ = std::max(max_match_, alternative->max_match());
  }
}

namespace {

int IncreaseBy(int previous, int increase) {
  if (RegExpTree::kInfinity - previous < increase) {
    return RegExpTree::kInfinity;
  } else {
    return previous + increase;
  }
}

}  // namespace

RegExpAlternative::RegExpAlternative(ZoneList<RegExpTree*>* nodes)
    : nodes_(nodes) {
  DCHECK_LT(1, nodes->length());
  min_match_ = 0;
  max_match_ = 0;
  for (int i = 0; i < nodes->length(); i++) {
    RegExpTree* node = nodes->at(i);
    int node_min_match = node->min_match();
    min_match_ = IncreaseBy(min_match_, node_min_match);
    int node_max_match = node->max_match();
    max_match_ = IncreaseBy(max_match_, node_max_match);
  }
}

RegExpClassSetOperand::RegExpClassSetOperand(ZoneList<CharacterRange>* ranges,
                                             CharacterClassStrings* strings)
    : ranges_(ranges), strings_(strings) {
  DCHECK_NOT_NULL(ranges);
  min_match_ = 0;
  max_match_ = 0;
  if (!ranges->is_empty()) {
    min_match_ = 1;
    max_match_ = 2;
  }
  if (has_strings()) {
    for (auto string : *strings) {
      min_match_ = std::min(min_match_, string.second->min_match());
      max_match_ = std::max(max_match_, string.second->max_match());
    }
  }
}

RegExpClassSetExpression::RegExpClassSetExpression(
    OperationType op, bool is_negated, bool may_contain_strings,
    ZoneList<RegExpTree*>* operands)
    : operation_(op),
      is_negated_(is_negated),
      may_contain_strings_(may_contain_strings),
      operands_(operands) {
  DCHECK_NOT_NULL(operands);
  if (is_negated) {
    DCHECK(!may_contain_strings_);
    // We don't know anything about max matches for negated classes.
    // As there are no strings involved, assume that we can match a unicode
    // character (2 code points).
    max_match_ = 2;
  } else {
    max_match_ = 0;
    for (auto operand : *operands) {
      max_match_ = std::max(max_match_, operand->max_match());
    }
  }
}

// static
RegExpClassSetExpression* RegExpClassSetExpression::Empty(Zone* zone,
                                                          bool is_negated) {
  ZoneList<CharacterRange>* ranges =
      zone->template New<ZoneList<CharacterRange>>(0, zone);
  RegExpClassSetOperand* op =
      zone->template New<RegExpClassSetOperand>(ranges, nullptr);
  ZoneList<RegExpTree*>* operands =
      zone->template New<ZoneList<RegExpTree*>>(1, zone);
  operands->Add(op, zone);
  return zone->template New<RegExpClassSetExpression>(
      RegExpClassSetExpression::OperationType::kUnion, is_negated, false,
      operands);
}

}  // namespace internal
}  // namespace v8
