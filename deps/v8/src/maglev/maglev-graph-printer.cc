// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_MAGLEV_GRAPH_PRINTER

#include "src/maglev/maglev-graph-printer.h"

#include <initializer_list>
#include <iomanip>
#include <ostream>
#include <type_traits>
#include <vector>

#include "src/base/logging.h"
#include "src/common/assert-scope.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-decoder.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
namespace maglev {

namespace {

int IntWidth(int val) { return std::ceil(std::log10(val + 1)); }

int MaxIdWidth(MaglevGraphLabeller* graph_labeller, NodeIdT max_node_id,
               int padding_adjustement = 0) {
  int max_width = IntWidth(graph_labeller->max_node_id());
  if (max_node_id != kInvalidNodeId) {
    max_width += IntWidth(max_node_id) + 1;
  }
  return max_width + 2 + padding_adjustement;
}

void PrintPaddedId(std::ostream& os, MaglevGraphLabeller* graph_labeller,
                   NodeIdT max_node_id, NodeBase* node,
                   std::string padding = " ", int padding_adjustement = 0) {
  int id = graph_labeller->NodeId(node);
  int id_width = IntWidth(id);
  int other_id_width = node->has_id() ? 1 + IntWidth(node->id()) : 0;
  int max_width = MaxIdWidth(graph_labeller, max_node_id, padding_adjustement);
  int padding_width = std::max(0, max_width - id_width - other_id_width);

  for (int i = 0; i < padding_width; ++i) {
    os << padding;
  }
  if (v8_flags.log_colour) os << "\033[0m";
  if (node->has_id()) {
    os << node->id() << "/";
  }
  os << graph_labeller->NodeId(node) << ": ";
}

void PrintPadding(std::ostream& os, int size) {
  os << std::setfill(' ') << std::setw(size) << "";
}

void PrintPadding(std::ostream& os, MaglevGraphLabeller* graph_labeller,
                  NodeIdT max_node_id, int padding_adjustement) {
  PrintPadding(os,
               MaxIdWidth(graph_labeller, max_node_id, padding_adjustement));
}

enum ConnectionLocation {
  kTop = 1 << 0,
  kLeft = 1 << 1,
  kRight = 1 << 2,
  kBottom = 1 << 3
};

struct Connection {
  void Connect(ConnectionLocation loc) { connected |= loc; }

  void AddHorizontal() {
    Connect(kLeft);
    Connect(kRight);
  }

  void AddVertical() {
    Connect(kTop);
    Connect(kBottom);
  }

  const char* ToString() const {
    switch (connected) {
      case 0:
        return " ";
      case kTop:
        return "╵";
      case kLeft:
        return "╴";
      case kRight:
        return "╶";
      case kBottom:
        return "╷";
      case kTop | kLeft:
        return "╯";
      case kTop | kRight:
        return "╰";
      case kBottom | kLeft:
        return "╮";
      case kBottom | kRight:
        return "╭";
      case kTop | kBottom:
        return "│";
      case kLeft | kRight:
        return "─";
      case kTop | kBottom | kLeft:
        return "┤";
      case kTop | kBottom | kRight:
        return "├";
      case kLeft | kRight | kTop:
        return "┴";
      case kLeft | kRight | kBottom:
        return "┬";
      case kTop | kLeft | kRight | kBottom:
        return "┼";
    }
    UNREACHABLE();
  }

  uint8_t connected = 0;
};

std::ostream& operator<<(std::ostream& os, const Connection& c) {
  return os << c.ToString();
}

// Print the vertical parts of connection arrows, optionally connecting arrows
// that were only first created on this line (passed in "arrows_starting_here")
// and should therefore connect rightwards instead of upwards.
void PrintVerticalArrows(std::ostream& os,
                         const std::vector<BasicBlock*>& targets,
                         std::set<size_t> arrows_starting_here = {},
                         std::set<BasicBlock*> targets_starting_here = {},
                         bool is_loop = false) {
  bool saw_start = false;
  int line_color = -1;
  int current_color = -1;
  for (size_t i = 0; i < targets.size(); ++i) {
    int desired_color = line_color;
    Connection c;
    if (saw_start) {
      c.AddHorizontal();
    }
    if (arrows_starting_here.find(i) != arrows_starting_here.end() ||
        targets_starting_here.find(targets[i]) != targets_starting_here.end()) {
      desired_color = (i % 6) + 1;
      line_color = desired_color;
      c.Connect(kRight);
      c.Connect(is_loop ? kTop : kBottom);
      saw_start = true;
    }

    // Only add the vertical connection if there was no other connection.
    if (c.connected == 0 && targets[i] != nullptr) {
      desired_color = (i % 6) + 1;
      c.AddVertical();
    }
    if (v8_flags.log_colour && desired_color != current_color &&
        desired_color != -1) {
      os << "\033[0;3" << desired_color << "m";
      current_color = desired_color;
    }
    os << c;
  }
  // If there are no arrows starting here, clear the color. Otherwise,
  // PrintPaddedId will clear it.
  if (v8_flags.log_colour && arrows_starting_here.empty() &&
      targets_starting_here.empty()) {
    os << "\033[0m";
  }
}

// Add a target to the target list in the first non-null position from the end.
// This might have to extend the target list if there is no free spot.
size_t AddTarget(std::vector<BasicBlock*>& targets, BasicBlock* target) {
  if (targets.size() == 0 || targets.back() != nullptr) {
    targets.push_back(target);
    return targets.size() - 1;
  }

  size_t i = targets.size();
  while (i > 0) {
    if (targets[i - 1] != nullptr) break;
    i--;
  }
  targets[i] = target;
  return i;
}

// If the target is not a fallthrough, add i to the target list in the first
// non-null position from the end. This might have to extend the target list if
// there is no free spot. Returns true if it was added, false if it was a
// fallthrough.
bool AddTargetIfNotNext(std::vector<BasicBlock*>& targets, BasicBlock* target,
                        BasicBlock* next_block,
                        std::set<size_t>* arrows_starting_here = nullptr) {
  if (next_block == target) return false;
  size_t index = AddTarget(targets, target);
  if (arrows_starting_here != nullptr) arrows_starting_here->insert(index);
  return true;
}

class MaglevPrintingVisitorOstream : public std::ostream,
                                     private std::streambuf {
 public:
  MaglevPrintingVisitorOstream(std::ostream& os,
                               std::vector<BasicBlock*>* targets)
      : std::ostream(this), os_(os), targets_(targets), padding_size_(0) {}
  ~MaglevPrintingVisitorOstream() override = default;

  static MaglevPrintingVisitorOstream* cast(
      const std::unique_ptr<std::ostream>& os) {
    return static_cast<MaglevPrintingVisitorOstream*>(os.get());
  }

  void set_padding(int padding_size) { padding_size_ = padding_size; }

 protected:
  int overflow(int c) override;

 private:
  std::ostream& os_;
  std::vector<BasicBlock*>* targets_;
  int padding_size_;
  bool previous_was_new_line_ = true;
};

int MaglevPrintingVisitorOstream::overflow(int c) {
  if (c == EOF) return c;

  if (previous_was_new_line_) {
    PrintVerticalArrows(os_, *targets_);
    PrintPadding(os_, padding_size_);
  }
  os_.rdbuf()->sputc(c);
  previous_was_new_line_ = (c == '\n');
  return c;
}

}  // namespace

MaglevPrintingVisitor::MaglevPrintingVisitor(
    MaglevGraphLabeller* graph_labeller, std::ostream& os)
    : graph_labeller_(graph_labeller),
      os_(os),
      os_for_additional_info_(
          new MaglevPrintingVisitorOstream(os_, &targets_)) {}

void MaglevPrintingVisitor::PreProcessGraph(Graph* graph) {
  os_ << "Graph\n\n";

  for (BasicBlock* block : *graph) {
    if (block->control_node()->Is<JumpLoop>()) {
      loop_headers_.insert(block->control_node()->Cast<JumpLoop>()->target());
    }
    if (max_node_id_ == kInvalidNodeId) {
      if (block->control_node()->has_id()) {
        max_node_id_ = block->control_node()->id();
      }
    } else {
      max_node_id_ = std::max(max_node_id_, block->control_node()->id());
    }
  }

  // Precalculate the maximum number of targets.
  for (BlockConstIterator block_it = graph->begin(); block_it != graph->end();
       ++block_it) {
    BasicBlock* block = *block_it;
    std::replace(targets_.begin(), targets_.end(), block,
                 static_cast<BasicBlock*>(nullptr));

    if (loop_headers_.find(block) != loop_headers_.end()) {
      AddTarget(targets_, block);
    }
    ControlNode* node = block->control_node();
    if (node->Is<JumpLoop>()) {
      BasicBlock* target = node->Cast<JumpLoop>()->target();
      std::replace(targets_.begin(), targets_.end(), target,
                   static_cast<BasicBlock*>(nullptr));
    } else if (node->Is<UnconditionalControlNode>()) {
      AddTargetIfNotNext(targets_,
                         node->Cast<UnconditionalControlNode>()->target(),
                         *(block_it + 1));
    } else if (node->Is<BranchControlNode>()) {
      AddTargetIfNotNext(targets_, node->Cast<BranchControlNode>()->if_true(),
                         *(block_it + 1));
      AddTargetIfNotNext(targets_, node->Cast<BranchControlNode>()->if_false(),
                         *(block_it + 1));
    } else if (node->Is<Switch>()) {
      for (int i = 0; i < node->Cast<Switch>()->size(); i++) {
        const BasicBlockRef& target = node->Cast<Switch>()->targets()[i];
        AddTargetIfNotNext(targets_, target.block_ptr(), *(block_it + 1));
      }
      if (node->Cast<Switch>()->has_fallthrough()) {
        BasicBlock* fallthrough_target = node->Cast<Switch>()->fallthrough();
        AddTargetIfNotNext(targets_, fallthrough_target, *(block_it + 1));
      }
    }
  }
  DCHECK(std::all_of(targets_.begin(), targets_.end(),
                     [](BasicBlock* block) { return block == nullptr; }));
}

void MaglevPrintingVisitor::PreProcessBasicBlock(BasicBlock* block) {
  size_t loop_position = static_cast<size_t>(-1);
  if (loop_headers_.erase(block) > 0) {
    loop_position = AddTarget(targets_, block);
  }
  {
    bool saw_start = false;
    int current_color = -1;
    int line_color = -1;
    for (size_t i = 0; i < targets_.size(); ++i) {
      int desired_color = line_color;
      Connection c;
      if (saw_start) {
        c.AddHorizontal();
      }
      // If this is one of the arrows pointing to this block, terminate the
      // line by connecting it rightwards.
      if (targets_[i] == block) {
        // Update the color of the line.
        desired_color = (i % 6) + 1;
        line_color = desired_color;
        c.Connect(kRight);
        // If this is the loop header, go down instead of up and don't clear
        // the target.
        if (i == loop_position) {
          c.Connect(kBottom);
        } else {
          c.Connect(kTop);
          targets_[i] = nullptr;
        }
        saw_start = true;
      } else if (c.connected == 0 && targets_[i] != nullptr) {
        // If this is another arrow, connect it, but only if that doesn't
        // clobber any existing drawing. Set the current color, but don't update
        // the overall color.
        desired_color = (i % 6) + 1;
        c.AddVertical();
      }
      if (v8_flags.log_colour && current_color != desired_color &&
          desired_color != -1) {
        os_ << "\033[0;3" << desired_color << "m";
        current_color = desired_color;
      }
      os_ << c;
    }
    os_ << (saw_start ? "►" : " ");
    if (v8_flags.log_colour) os_ << "\033[0m";
  }

  int block_id = graph_labeller_->BlockId(block);
  os_ << "Block b" << block_id;
  if (block->is_exception_handler_block()) {
    os_ << " (exception handler)";
  }
  os_ << "\n";

  MaglevPrintingVisitorOstream::cast(os_for_additional_info_)->set_padding(1);
}

namespace {

void PrintSingleDeoptFrame(
    std::ostream& os, MaglevGraphLabeller* graph_labeller,
    const DeoptFrame& frame, InputLocation*& current_input_location,
    LazyDeoptInfo* lazy_deopt_info_if_top_frame = nullptr) {
  switch (frame.type()) {
    case DeoptFrame::FrameType::kInterpretedFrame: {
      os << "@" << frame.as_interpreted().bytecode_position();
      if (!v8_flags.print_maglev_deopt_verbose) {
        int count = 0;
        frame.as_interpreted().frame_state()->ForEachValue(
            frame.as_interpreted().unit(),
            [&](ValueNode* node, interpreter::Register reg) { count++; });
        os << " (" << count << " live vars)";
        return;
      }
      os << " : {";
      os << "<closure>:"
         << PrintNodeLabel(graph_labeller, frame.as_interpreted().closure())
         << ":" << current_input_location->operand();
      current_input_location++;
      frame.as_interpreted().frame_state()->ForEachValue(
          frame.as_interpreted().unit(),
          [&](ValueNode* node, interpreter::Register reg) {
            os << ", " << reg.ToString() << ":";
            if (lazy_deopt_info_if_top_frame &&
                lazy_deopt_info_if_top_frame->IsResultRegister(reg)) {
              os << "<result>";
            } else {
              os << PrintNodeLabel(graph_labeller, node) << ":"
                 << current_input_location->operand();
              current_input_location++;
            }
          });
      os << "}";
      break;
    }
    case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
      os << "@ConstructInvokeStub";
      if (!v8_flags.print_maglev_deopt_verbose) return;
      os << " : {";
      os << "<this>:"
         << PrintNodeLabel(graph_labeller, frame.as_construct_stub().receiver())
         << ":" << current_input_location->operand();
      current_input_location++;
      os << ", <context>:"
         << PrintNodeLabel(graph_labeller, frame.as_construct_stub().context())
         << ":" << current_input_location->operand();
      current_input_location++;
      os << "}";
      break;
    }
    case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
      os << "@" << frame.as_inlined_arguments().bytecode_position();
      if (!v8_flags.print_maglev_deopt_verbose) return;
      os << " : {";
      auto arguments = frame.as_inlined_arguments().arguments();
      DCHECK_GT(arguments.size(), 0);
      os << "<this>:" << PrintNodeLabel(graph_labeller, arguments[0]) << ":"
         << current_input_location->operand();
      current_input_location++;
      if (arguments.size() > 1) {
        os << ", ";
      }
      for (size_t i = 1; i < arguments.size(); i++) {
        os << "a" << (i - 1) << ":"
           << PrintNodeLabel(graph_labeller, arguments[i]) << ":"
           << current_input_location->operand();
        current_input_location++;
        os << ", ";
      }
      os << "}";
      break;
    }
    case DeoptFrame::FrameType::kBuiltinContinuationFrame: {
      os << "@" << Builtins::name(frame.as_builtin_continuation().builtin_id());
      if (!v8_flags.print_maglev_deopt_verbose) return;
      os << " : {";
      int arg_index = 0;
      for (ValueNode* node : frame.as_builtin_continuation().parameters()) {
        os << "a" << arg_index << ":" << PrintNodeLabel(graph_labeller, node)
           << ":" << current_input_location->operand();
        arg_index++;
        current_input_location++;
        os << ", ";
      }
      os << "<context>:"
         << PrintNodeLabel(graph_labeller,
                           frame.as_builtin_continuation().context())
         << ":" << current_input_location->operand();
      current_input_location++;
      os << "}";
      break;
    }
  }
}

void RecursivePrintEagerDeopt(std::ostream& os,
                              std::vector<BasicBlock*> targets,
                              const DeoptFrame& frame,
                              MaglevGraphLabeller* graph_labeller,
                              int max_node_id,
                              InputLocation*& current_input_location) {
  if (frame.parent()) {
    RecursivePrintEagerDeopt(os, targets, *frame.parent(), graph_labeller,
                             max_node_id, current_input_location);
  }

  PrintVerticalArrows(os, targets);
  PrintPadding(os, graph_labeller, max_node_id, 0);
  if (!frame.parent()) {
    os << "  ↱ eager ";
  } else {
    os << "  │       ";
  }
  PrintSingleDeoptFrame(os, graph_labeller, frame, current_input_location);
  os << "\n";
}

void PrintEagerDeopt(std::ostream& os, std::vector<BasicBlock*> targets,
                     NodeBase* node, MaglevGraphLabeller* graph_labeller,
                     int max_node_id) {
  EagerDeoptInfo* deopt_info = node->eager_deopt_info();
  InputLocation* current_input_location = deopt_info->input_locations();
  RecursivePrintEagerDeopt(os, targets, deopt_info->top_frame(), graph_labeller,
                           max_node_id, current_input_location);
}

void MaybePrintEagerDeopt(std::ostream& os, std::vector<BasicBlock*> targets,
                          NodeBase* node, MaglevGraphLabeller* graph_labeller,
                          int max_node_id) {
  if (node->properties().can_eager_deopt()) {
    PrintEagerDeopt(os, targets, node, graph_labeller, max_node_id);
  }
}

void RecursivePrintLazyDeopt(std::ostream& os, std::vector<BasicBlock*> targets,
                             const DeoptFrame& frame,
                             MaglevGraphLabeller* graph_labeller,
                             int max_node_id,
                             InputLocation*& current_input_location) {
  if (frame.parent()) {
    RecursivePrintLazyDeopt(os, targets, *frame.parent(), graph_labeller,
                            max_node_id, current_input_location);
  }

  PrintVerticalArrows(os, targets);
  PrintPadding(os, graph_labeller, max_node_id, 0);
  os << "  │      ";
  PrintSingleDeoptFrame(os, graph_labeller, frame, current_input_location);
  os << "\n";
}

template <typename NodeT>
void PrintLazyDeopt(std::ostream& os, std::vector<BasicBlock*> targets,
                    NodeT* node, MaglevGraphLabeller* graph_labeller,
                    int max_node_id) {
  LazyDeoptInfo* deopt_info = node->lazy_deopt_info();
  InputLocation* current_input_location = deopt_info->input_locations();
  const DeoptFrame& top_frame = deopt_info->top_frame();
  if (top_frame.parent()) {
    RecursivePrintLazyDeopt(os, targets, *top_frame.parent(), graph_labeller,
                            max_node_id, current_input_location);
  }

  PrintVerticalArrows(os, targets);
  PrintPadding(os, graph_labeller, max_node_id, 0);

  os << "  ↳ lazy ";
  PrintSingleDeoptFrame(os, graph_labeller, top_frame, current_input_location,
                        deopt_info);
  os << "\n";
}

template <typename NodeT>
void PrintExceptionHandlerPoint(std::ostream& os,
                                std::vector<BasicBlock*> targets, NodeT* node,
                                MaglevGraphLabeller* graph_labeller,
                                int max_node_id) {
  // If no handler info, then we cannot throw.
  ExceptionHandlerInfo* info = node->exception_handler_info();
  if (!info->HasExceptionHandler()) return;

  BasicBlock* block = info->catch_block.block_ptr();
  DCHECK(block->is_exception_handler_block());

  if (!block->has_phi()) {
    return;
  }
  Phi* first_phi = block->phis()->first();
  CHECK_NOT_NULL(first_phi);
  int handler_offset = first_phi->merge_state()->merge_offset();

  // The exception handler liveness should be a subset of lazy_deopt_info one.
  auto* liveness = block->state()->frame_state().liveness();
  LazyDeoptInfo* deopt_info = node->lazy_deopt_info();

  const InterpretedDeoptFrame* lazy_frame;
  switch (deopt_info->top_frame().type()) {
    case DeoptFrame::FrameType::kInterpretedFrame:
      lazy_frame = &deopt_info->top_frame().as_interpreted();
      break;
    case DeoptFrame::FrameType::kInlinedArgumentsFrame:
      UNREACHABLE();
    case DeoptFrame::FrameType::kConstructInvokeStubFrame:
    case DeoptFrame::FrameType::kBuiltinContinuationFrame:
      lazy_frame = &deopt_info->top_frame().parent()->as_interpreted();
      break;
  }

  PrintVerticalArrows(os, targets);
  PrintPadding(os, graph_labeller, max_node_id, 0);

  os << "  ↳ throw @" << handler_offset << " : {";
  bool first = true;
  lazy_frame->as_interpreted().frame_state()->ForEachValue(
      lazy_frame->as_interpreted().unit(),
      [&](ValueNode* node, interpreter::Register reg) {
        if (!reg.is_parameter() && !liveness->RegisterIsLive(reg.index())) {
          // Skip, since not live at the handler offset.
          return;
        }
        if (first) {
          first = false;
        } else {
          os << ", ";
        }
        os << reg.ToString() << ":" << PrintNodeLabel(graph_labeller, node);
      });
  os << "}\n";
}

void MaybePrintLazyDeoptOrExceptionHandler(std::ostream& os,
                                           std::vector<BasicBlock*> targets,
                                           NodeBase* node,
                                           MaglevGraphLabeller* graph_labeller,
                                           int max_node_id) {
  switch (node->opcode()) {
#define CASE(Name)                                                          \
  case Opcode::k##Name:                                                     \
    if constexpr (Name::kProperties.can_lazy_deopt()) {                     \
      PrintLazyDeopt<Name>(os, targets, node->Cast<Name>(), graph_labeller, \
                           max_node_id);                                    \
    }                                                                       \
    if constexpr (Name::kProperties.can_throw()) {                          \
      PrintExceptionHandlerPoint<Name>(os, targets, node->Cast<Name>(),     \
                                       graph_labeller, max_node_id);        \
    }                                                                       \
    break;
    NODE_BASE_LIST(CASE)
#undef CASE
  }
}

void MaybePrintProvenance(std::ostream& os, std::vector<BasicBlock*> targets,
                          MaglevGraphLabeller::Provenance provenance,
                          MaglevGraphLabeller::Provenance existing_provenance) {
  DisallowGarbageCollection no_gc;

  // Print function every time the compilation unit changes.
  bool needs_function_print = provenance.unit != existing_provenance.unit;
  Tagged<Script> script;
  Script::PositionInfo position_info;
  bool has_position_info = false;

  // Print position inside function every time either the position or the
  // compilation unit changes.
  if (provenance.position.IsKnown() &&
      (provenance.position != existing_provenance.position ||
       provenance.unit != existing_provenance.unit)) {
    script = Script::cast(
        provenance.unit->shared_function_info().object()->script());
    has_position_info = script->GetPositionInfo(
        provenance.position.ScriptOffset(), &position_info,
        Script::OffsetFlag::kWithOffset);
    needs_function_print = true;
  }

  // Do the actual function + position print.
  if (needs_function_print) {
    if (script.is_null()) {
      script = Script::cast(
          provenance.unit->shared_function_info().object()->script());
    }
    PrintVerticalArrows(os, targets);
    if (v8_flags.log_colour) {
      os << "\033[1;34m";
    }
    os << *provenance.unit->shared_function_info().object() << " ("
       << script->GetNameOrSourceURL();
    if (has_position_info) {
      os << ":" << position_info.line << ":" << position_info.column;
    } else if (provenance.position.IsKnown()) {
      os << "@" << provenance.position.ScriptOffset();
    }
    os << ")\n";
    if (v8_flags.log_colour) {
      os << "\033[m";
    }
  }

  // Print current bytecode every time the offset or current compilation unit
  // (i.e. bytecode array) changes.
  if (!provenance.bytecode_offset.IsNone() &&
      (provenance.bytecode_offset != existing_provenance.bytecode_offset ||
       provenance.unit != existing_provenance.unit)) {
    PrintVerticalArrows(os, targets);

    interpreter::BytecodeArrayIterator iterator(
        provenance.unit->bytecode().object(),
        provenance.bytecode_offset.ToInt(), no_gc);
    if (v8_flags.log_colour) {
      os << "\033[0;34m";
    }
    os << std::setw(4) << iterator.current_offset() << " : ";
    interpreter::BytecodeDecoder::Decode(os, iterator.current_address(), false);
    os << "\n";
    if (v8_flags.log_colour) {
      os << "\033[m";
    }
  }
}

}  // namespace

ProcessResult MaglevPrintingVisitor::Process(Phi* phi,
                                             const ProcessingState& state) {
  PrintVerticalArrows(os_, targets_);
  PrintPaddedId(os_, graph_labeller_, max_node_id_, phi);
  os_ << "φ";
  switch (phi->value_representation()) {
    case ValueRepresentation::kTagged:
      os_ << "ᵀ";
      break;
    case ValueRepresentation::kInt32:
      os_ << "ᴵ";
      break;
    case ValueRepresentation::kUint32:
      os_ << "ᵁ";
      break;
    case ValueRepresentation::kFloat64:
      os_ << "ᶠ";
      break;
    case ValueRepresentation::kHoleyFloat64:
      os_ << "ʰᶠ";
      break;
    case ValueRepresentation::kIntPtr:
      UNREACHABLE();
  }
  if (phi->input_count() == 0) {
    os_ << "ₑ " << phi->owner().ToString();
  } else {
    os_ << " " << phi->owner().ToString() << " (";
    // Manually walk Phi inputs to print just the node labels, without
    // input locations (which are shown in the predecessor block's gap
    // moves).
    for (int i = 0; i < phi->input_count(); ++i) {
      if (i > 0) os_ << ", ";
      os_ << PrintNodeLabel(graph_labeller_, phi->input(i).node());
    }
    os_ << ")";
  }
  if (phi->is_tagged() && !phi->result().operand().IsUnallocated()) {
    if (phi->decompresses_tagged_result()) {
      os_ << " (decompressed)";
    } else {
      os_ << " (compressed)";
    }
  }
  os_ << " → " << phi->result().operand();
  if (phi->has_valid_live_range()) {
    os_ << ", live range: [" << phi->live_range().start << "-"
        << phi->live_range().end << "]";
  }
  if (!phi->has_id()) {
    os_ << ", " << phi->use_count() << " uses";
  }
  os_ << "\n";

  MaglevPrintingVisitorOstream::cast(os_for_additional_info_)
      ->set_padding(MaxIdWidth(graph_labeller_, max_node_id_, 2));
  return ProcessResult::kContinue;
}

ProcessResult MaglevPrintingVisitor::Process(Node* node,
                                             const ProcessingState& state) {
  MaglevGraphLabeller::Provenance provenance =
      graph_labeller_->GetNodeProvenance(node);
  if (provenance.unit != nullptr) {
    MaybePrintProvenance(os_, targets_, provenance, existing_provenance_);
    existing_provenance_ = provenance;
  }

  MaybePrintEagerDeopt(os_, targets_, node, graph_labeller_, max_node_id_);

  PrintVerticalArrows(os_, targets_);
  PrintPaddedId(os_, graph_labeller_, max_node_id_, node);
  if (node->properties().is_call()) {
    os_ << "🐢 ";
  }
  os_ << PrintNode(graph_labeller_, node) << "\n";

  MaglevPrintingVisitorOstream::cast(os_for_additional_info_)
      ->set_padding(MaxIdWidth(graph_labeller_, max_node_id_, 2));

  MaybePrintLazyDeoptOrExceptionHandler(os_, targets_, node, graph_labeller_,
                                        max_node_id_);
  return ProcessResult::kContinue;
}

ProcessResult MaglevPrintingVisitor::Process(ControlNode* control_node,
                                             const ProcessingState& state) {
  MaglevGraphLabeller::Provenance provenance =
      graph_labeller_->GetNodeProvenance(control_node);
  if (provenance.unit != nullptr) {
    MaybePrintProvenance(os_, targets_, provenance, existing_provenance_);
    existing_provenance_ = provenance;
  }

  MaybePrintEagerDeopt(os_, targets_, control_node, graph_labeller_,
                       max_node_id_);

  bool has_fallthrough = false;

  if (control_node->Is<JumpLoop>()) {
    BasicBlock* target = control_node->Cast<JumpLoop>()->target();

    PrintVerticalArrows(os_, targets_, {}, {target}, true);
    os_ << "◄─";
    PrintPaddedId(os_, graph_labeller_, max_node_id_, control_node, "─", -2);
    std::replace(targets_.begin(), targets_.end(), target,
                 static_cast<BasicBlock*>(nullptr));

  } else if (control_node->Is<UnconditionalControlNode>()) {
    BasicBlock* target =
        control_node->Cast<UnconditionalControlNode>()->target();

    std::set<size_t> arrows_starting_here;
    has_fallthrough |= !AddTargetIfNotNext(targets_, target, state.next_block(),
                                           &arrows_starting_here);
    PrintVerticalArrows(os_, targets_, arrows_starting_here);
    PrintPaddedId(os_, graph_labeller_, max_node_id_, control_node,
                  has_fallthrough ? " " : "─");

  } else if (control_node->Is<BranchControlNode>()) {
    BasicBlock* true_target =
        control_node->Cast<BranchControlNode>()->if_true();
    BasicBlock* false_target =
        control_node->Cast<BranchControlNode>()->if_false();

    std::set<size_t> arrows_starting_here;
    has_fallthrough |= !AddTargetIfNotNext(
        targets_, false_target, state.next_block(), &arrows_starting_here);
    has_fallthrough |= !AddTargetIfNotNext(
        targets_, true_target, state.next_block(), &arrows_starting_here);
    PrintVerticalArrows(os_, targets_, arrows_starting_here);
    PrintPaddedId(os_, graph_labeller_, max_node_id_, control_node, "─");
  } else if (control_node->Is<Switch>()) {
    std::set<size_t> arrows_starting_here;
    for (int i = 0; i < control_node->Cast<Switch>()->size(); i++) {
      const BasicBlockRef& target = control_node->Cast<Switch>()->targets()[i];
      has_fallthrough |=
          !AddTargetIfNotNext(targets_, target.block_ptr(), state.next_block(),
                              &arrows_starting_here);
    }

    if (control_node->Cast<Switch>()->has_fallthrough()) {
      BasicBlock* fallthrough_target =
          control_node->Cast<Switch>()->fallthrough();
      has_fallthrough |=
          !AddTargetIfNotNext(targets_, fallthrough_target, state.next_block(),
                              &arrows_starting_here);
    }

    PrintVerticalArrows(os_, targets_, arrows_starting_here);
    PrintPaddedId(os_, graph_labeller_, max_node_id_, control_node, "─");

  } else {
    PrintVerticalArrows(os_, targets_);
    PrintPaddedId(os_, graph_labeller_, max_node_id_, control_node);
  }

  os_ << PrintNode(graph_labeller_, control_node) << "\n";

  bool printed_phis = false;
  if (control_node->Is<UnconditionalControlNode>()) {
    BasicBlock* target =
        control_node->Cast<UnconditionalControlNode>()->target();
    if (target->has_phi()) {
      printed_phis = true;
      PrintVerticalArrows(os_, targets_);
      PrintPadding(os_, graph_labeller_, max_node_id_, -1);
      os_ << (has_fallthrough ? "│" : " ");
      os_ << "  with gap moves:\n";
      int pid = state.block()->predecessor_id();
      for (Phi* phi : *target->phis()) {
        PrintVerticalArrows(os_, targets_);
        PrintPadding(os_, graph_labeller_, max_node_id_, -1);
        os_ << (has_fallthrough ? "│" : " ");
        os_ << "    - ";
        graph_labeller_->PrintInput(os_, phi->input(pid));
        os_ << " → " << graph_labeller_->NodeId(phi) << ": φ";
        switch (phi->value_representation()) {
          case ValueRepresentation::kTagged:
            os_ << "ᵀ";
            break;
          case ValueRepresentation::kInt32:
            os_ << "ᴵ";
            break;
          case ValueRepresentation::kUint32:
            os_ << "ᵁ";
            break;
          case ValueRepresentation::kFloat64:
            os_ << "ᶠ";
            break;
          case ValueRepresentation::kHoleyFloat64:
            os_ << "ʰᶠ";
            break;
          case ValueRepresentation::kIntPtr:
            UNREACHABLE();
        }
        os_ << " " << phi->owner().ToString() << " " << phi->result().operand()
            << "\n";
      }
#ifdef V8_ENABLE_MAGLEV
      if (target->state()->register_state().is_initialized()) {
        PrintVerticalArrows(os_, targets_);
        PrintPadding(os_, graph_labeller_, max_node_id_, -1);
        os_ << (has_fallthrough ? "│" : " ");
        os_ << "  with register merges:\n";
        auto print_register_merges = [&](auto reg, RegisterState& state) {
          ValueNode* node;
          RegisterMerge* merge;
          if (LoadMergeState(state, &node, &merge)) {
            compiler::InstructionOperand source = merge->operand(pid);
            PrintVerticalArrows(os_, targets_);
            PrintPadding(os_, graph_labeller_, max_node_id_, -1);
            os_ << (has_fallthrough ? "│" : " ");
            os_ << "    - " << source << " → " << reg << "\n";
          }
        };
        target->state()->register_state().ForEachGeneralRegister(
            print_register_merges);
        target->state()->register_state().ForEachDoubleRegister(
            print_register_merges);
      }
#endif
    }
  }

  PrintVerticalArrows(os_, targets_);
  if (has_fallthrough) {
    PrintPadding(os_, graph_labeller_, max_node_id_, -1);
    if (printed_phis) {
      os_ << "▼";
    } else {
      os_ << "↓";
    }
  }
  os_ << "\n";

  // TODO(leszeks): Allow MaglevPrintingVisitorOstream to print the arrowhead
  // so that it overlaps the fallthrough arrow.
  MaglevPrintingVisitorOstream::cast(os_for_additional_info_)
      ->set_padding(MaxIdWidth(graph_labeller_, max_node_id_, 2));

  return ProcessResult::kContinue;
}

void PrintGraph(std::ostream& os, MaglevCompilationInfo* compilation_info,
                Graph* const graph) {
  GraphProcessor<MaglevPrintingVisitor, /*visit_identity_nodes*/ true> printer(
      compilation_info->graph_labeller(), os);
  printer.ProcessGraph(graph);
}

void PrintNode::Print(std::ostream& os) const {
  node_->Print(os, graph_labeller_, skip_targets_);
}

void PrintNodeLabel::Print(std::ostream& os) const {
  graph_labeller_->PrintNodeLabel(os, node_);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV_GRAPH_PRINTER
