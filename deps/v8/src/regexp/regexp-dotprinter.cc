// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-dotprinter.h"

#include "src/base/strings.h"
#include "src/regexp/regexp-compiler.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------
// Dot/dotty output

class DotPrinterImpl : public NodeVisitor {
 public:
  explicit DotPrinterImpl(std::ostream& os) : os_(os) {}
  void PrintNode(const char* label, RegExpNode* node);
  void Visit(RegExpNode* node);
  void PrintAttributes(RegExpNode* from);
  void PrintOnFailure(RegExpNode* from, RegExpNode* to);
#define DECLARE_VISIT(Type) virtual void Visit##Type(Type##Node* that);
  FOR_EACH_NODE_TYPE(DECLARE_VISIT)
#undef DECLARE_VISIT
 private:
  std::ostream& os_;
};

void DotPrinterImpl::PrintNode(const char* label, RegExpNode* node) {
  os_ << "digraph G {\n  graph [label=\"";
  for (int i = 0; label[i]; i++) {
    switch (label[i]) {
      case '\\':
        os_ << "\\\\";
        break;
      case '"':
        os_ << "\"";
        break;
      default:
        os_ << label[i];
        break;
    }
  }
  os_ << "\"];\n";
  Visit(node);
  os_ << "}" << std::endl;
}

void DotPrinterImpl::Visit(RegExpNode* node) {
  if (node->info()->visited) return;
  node->info()->visited = true;
  node->Accept(this);
}

void DotPrinterImpl::PrintOnFailure(RegExpNode* from, RegExpNode* on_failure) {
  os_ << "  n" << from << " -> n" << on_failure << " [style=dotted];\n";
  Visit(on_failure);
}

class AttributePrinter {
 public:
  explicit AttributePrinter(std::ostream& os) : os_(os), first_(true) {}
  void PrintSeparator() {
    if (first_) {
      first_ = false;
    } else {
      os_ << "|";
    }
  }
  void PrintBit(const char* name, bool value) {
    if (!value) return;
    PrintSeparator();
    os_ << "{" << name << "}";
  }
  void PrintPositive(const char* name, int value) {
    if (value < 0) return;
    PrintSeparator();
    os_ << "{" << name << "|" << value << "}";
  }

 private:
  std::ostream& os_;
  bool first_;
};

void DotPrinterImpl::PrintAttributes(RegExpNode* that) {
  os_ << "  a" << that << " [shape=Mrecord, color=grey, fontcolor=grey, "
      << "margin=0.1, fontsize=10, label=\"{";
  AttributePrinter printer(os_);
  NodeInfo* info = that->info();
  printer.PrintBit("NI", info->follows_newline_interest);
  printer.PrintBit("WI", info->follows_word_interest);
  printer.PrintBit("SI", info->follows_start_interest);
  Label* label = that->label();
  if (label->is_bound()) printer.PrintPositive("@", label->pos());
  os_ << "}\"];\n"
      << "  a" << that << " -> n" << that
      << " [style=dashed, color=grey, arrowhead=none];\n";
}

void DotPrinterImpl::VisitChoice(ChoiceNode* that) {
  os_ << "  n" << that << " [shape=Mrecord, label=\"?\"];\n";
  for (int i = 0; i < that->alternatives()->length(); i++) {
    GuardedAlternative alt = that->alternatives()->at(i);
    os_ << "  n" << that << " -> n" << alt.node();
  }
  for (int i = 0; i < that->alternatives()->length(); i++) {
    GuardedAlternative alt = that->alternatives()->at(i);
    alt.node()->Accept(this);
  }
}

void DotPrinterImpl::VisitLoopChoice(LoopChoiceNode* that) {
  VisitChoice(that);
}

void DotPrinterImpl::VisitNegativeLookaroundChoice(
    NegativeLookaroundChoiceNode* that) {
  VisitChoice(that);
}

void DotPrinterImpl::VisitText(TextNode* that) {
  Zone* zone = that->zone();
  os_ << "  n" << that << " [label=\"";
  for (int i = 0; i < that->elements()->length(); i++) {
    if (i > 0) os_ << " ";
    TextElement elm = that->elements()->at(i);
    switch (elm.text_type()) {
      case TextElement::ATOM: {
        base::Vector<const base::uc16> data = elm.atom()->data();
        for (int j = 0; j < data.length(); j++) {
          os_ << static_cast<char>(data[j]);
        }
        break;
      }
      case TextElement::CLASS_RANGES: {
        RegExpClassRanges* node = elm.class_ranges();
        os_ << "[";
        if (node->is_negated()) os_ << "^";
        for (int j = 0; j < node->ranges(zone)->length(); j++) {
          CharacterRange range = node->ranges(zone)->at(j);
          os_ << AsUC32(range.from()) << "-" << AsUC32(range.to());
        }
        os_ << "]";
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  os_ << "\", shape=box, peripheries=2];\n";
  PrintAttributes(that);
  os_ << "  n" << that << " -> n" << that->on_success() << ";\n";
  Visit(that->on_success());
}

void DotPrinterImpl::VisitBackReference(BackReferenceNode* that) {
  os_ << "  n" << that << " [label=\"$" << that->start_register() << "..$"
      << that->end_register() << "\", shape=doubleoctagon];\n";
  PrintAttributes(that);
  os_ << "  n" << that << " -> n" << that->on_success() << ";\n";
  Visit(that->on_success());
}

void DotPrinterImpl::VisitEnd(EndNode* that) {
  os_ << "  n" << that << " [style=bold, shape=point];\n";
  PrintAttributes(that);
}

void DotPrinterImpl::VisitAssertion(AssertionNode* that) {
  os_ << "  n" << that << " [";
  switch (that->assertion_type()) {
    case AssertionNode::AT_END:
      os_ << "label=\"$\", shape=septagon";
      break;
    case AssertionNode::AT_START:
      os_ << "label=\"^\", shape=septagon";
      break;
    case AssertionNode::AT_BOUNDARY:
      os_ << "label=\"\\b\", shape=septagon";
      break;
    case AssertionNode::AT_NON_BOUNDARY:
      os_ << "label=\"\\B\", shape=septagon";
      break;
    case AssertionNode::AFTER_NEWLINE:
      os_ << "label=\"(?<=\\n)\", shape=septagon";
      break;
  }
  os_ << "];\n";
  PrintAttributes(that);
  RegExpNode* successor = that->on_success();
  os_ << "  n" << that << " -> n" << successor << ";\n";
  Visit(successor);
}

void DotPrinterImpl::VisitAction(ActionNode* that) {
  os_ << "  n" << that << " [";
  switch (that->action_type_) {
    case ActionNode::SET_REGISTER_FOR_LOOP:
      os_ << "label=\"$" << that->data_.u_store_register.reg
          << ":=" << that->data_.u_store_register.value << "\", shape=octagon";
      break;
    case ActionNode::INCREMENT_REGISTER:
      os_ << "label=\"$" << that->data_.u_increment_register.reg
          << "++\", shape=octagon";
      break;
    case ActionNode::STORE_POSITION:
      os_ << "label=\"$" << that->data_.u_position_register.reg
          << ":=$pos\", shape=octagon";
      break;
    case ActionNode::BEGIN_POSITIVE_SUBMATCH:
      os_ << "label=\"$" << that->data_.u_submatch.current_position_register
          << ":=$pos,begin-positive\", shape=septagon";
      break;
    case ActionNode::BEGIN_NEGATIVE_SUBMATCH:
      os_ << "label=\"$" << that->data_.u_submatch.current_position_register
          << ":=$pos,begin-negative\", shape=septagon";
      break;
    case ActionNode::POSITIVE_SUBMATCH_SUCCESS:
      os_ << "label=\"escape\", shape=septagon";
      break;
    case ActionNode::EMPTY_MATCH_CHECK:
      os_ << "label=\"$" << that->data_.u_empty_match_check.start_register
          << "=$pos?,$" << that->data_.u_empty_match_check.repetition_register
          << "<" << that->data_.u_empty_match_check.repetition_limit
          << "?\", shape=septagon";
      break;
    case ActionNode::CLEAR_CAPTURES: {
      os_ << "label=\"clear $" << that->data_.u_clear_captures.range_from
          << " to $" << that->data_.u_clear_captures.range_to
          << "\", shape=septagon";
      break;
    }
    case ActionNode::MODIFY_FLAGS: {
      os_ << "label=\"flags $" << that->flags() << "\", shape=septagon";
      break;
    }
  }
  os_ << "];\n";
  PrintAttributes(that);
  RegExpNode* successor = that->on_success();
  os_ << "  n" << that << " -> n" << successor << ";\n";
  Visit(successor);
}

void DotPrinter::DotPrint(const char* label, RegExpNode* node) {
  StdoutStream os;
  DotPrinterImpl printer(os);
  printer.PrintNode(label, node);
}

}  // namespace internal
}  // namespace v8
