// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-visualizer.h"

#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "src/base/vector.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/source-position.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/backend/register-allocation.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/graph.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-disassembler.h"
#endif

namespace v8 {
namespace internal {
namespace compiler {

const char* get_cached_trace_turbo_filename(OptimizedCompilationInfo* info) {
  if (!info->trace_turbo_filename()) {
    info->set_trace_turbo_filename(GetVisualizerLogFileName(
        info, v8_flags.trace_turbo_path, nullptr, "json"));
  }
  return info->trace_turbo_filename();
}

TurboJsonFile::TurboJsonFile(OptimizedCompilationInfo* info,
                             std::ios_base::openmode mode)
    : std::ofstream(get_cached_trace_turbo_filename(info), mode) {}

TurboJsonFile::~TurboJsonFile() { flush(); }

TurboCfgFile::TurboCfgFile(Isolate* isolate)
    : std::ofstream(Isolate::GetTurboCfgFileName(isolate).c_str(),
                    std::ios_base::app) {}

TurboCfgFile::~TurboCfgFile() { flush(); }

std::ostream& operator<<(std::ostream& out,
                         const SourcePositionAsJSON& asJSON) {
  asJSON.sp.PrintJson(out);
  return out;
}

std::ostream& operator<<(std::ostream& out, const NodeOriginAsJSON& asJSON) {
  asJSON.no.PrintJson(out);
  return out;
}

void JsonPrintBytecodeSource(std::ostream& os, int source_id,
                             std::unique_ptr<char[]> function_name,
                             DirectHandle<BytecodeArray> bytecode_array) {
  os << "\"" << source_id << "\" : {";
  os << "\"sourceId\": " << source_id;
  os << ", \"functionName\": \"" << function_name.get() << "\"";
  os << ", \"bytecodeSource\": ";
  bytecode_array->PrintJson(os);
  os << "}";
}

void JsonPrintFunctionSource(std::ostream& os, int source_id,
                             std::unique_ptr<char[]> function_name,
                             Handle<Script> script, Isolate* isolate,
                             Handle<SharedFunctionInfo> shared, bool with_key) {
  if (with_key) os << "\"" << source_id << "\" : ";

  os << "{ ";
  os << "\"sourceId\": " << source_id;
  os << ", \"functionName\": \"" << function_name.get() << "\" ";

  int start = 0;
  int end = 0;
  if (!script.is_null() && !IsUndefined(*script, isolate) &&
      !shared.is_null()) {
    Tagged<Object> source_name = script->name();
    os << ", \"sourceName\": \"";
    if (IsString(source_name)) {
      std::ostringstream escaped_name;
      escaped_name << Cast<String>(source_name)->ToCString().get();
      os << JSONEscaped(escaped_name);
    }
    os << "\"";
    {
      start = shared->StartPosition();
      end = shared->EndPosition();
      os << ", \"sourceText\": \"";
      if (!IsUndefined(script->source())) {
        DisallowGarbageCollection no_gc;
        int len = shared->EndPosition() - start;
        SubStringRange source(Cast<String>(script->source()), no_gc, start,
                              len);
        for (auto c : source) {
          os << AsEscapedUC16ForJSON(c);
        }
#if V8_ENABLE_WEBASSEMBLY
      } else if (shared->HasWasmExportedFunctionData()) {
        Tagged<WasmExportedFunctionData> function_data =
            shared->wasm_exported_function_data();
        wasm::NativeModule* native_module =
            function_data->instance_data()->native_module();
        const wasm::WasmModule* module = native_module->module();
        std::ostringstream str;
        wasm::DisassembleFunction(module, function_data->function_index(),
                                  native_module->wire_bytes(),
                                  native_module->GetNamesProvider(), str);
        os << JSONEscaped(str);
#endif  // V8_ENABLE_WEBASSEMBLY
      }
      os << "\"";
    }
  } else {
    os << ", \"sourceName\": \"\"";
    os << ", \"sourceText\": \"\"";
  }
  os << ", \"startPosition\": " << start;
  os << ", \"endPosition\": " << end;
  os << "}";
}

int SourceIdAssigner::GetIdFor(Handle<SharedFunctionInfo> shared) {
  for (unsigned i = 0; i < printed_.size(); i++) {
    if (printed_.at(i).is_identical_to(shared)) {
      source_ids_.push_back(i);
      return i;
    }
  }
  const int source_id = static_cast<int>(printed_.size());
  printed_.push_back(shared);
  source_ids_.push_back(source_id);
  return source_id;
}

namespace {

void JsonPrintInlinedFunctionInfo(
    std::ostream& os, int source_id, int inlining_id,
    const OptimizedCompilationInfo::InlinedFunctionHolder& h) {
  os << "\"" << inlining_id << "\" : ";
  os << "{ \"inliningId\" : " << inlining_id;
  os << ", \"sourceId\" : " << source_id;
  const SourcePosition position = h.position.position;
  if (position.IsKnown()) {
    os << ", \"inliningPosition\" : " << AsJSON(position);
  }
  os << "}";
}

}  // namespace

void JsonPrintAllBytecodeSources(std::ostream& os,
                                 OptimizedCompilationInfo* info) {
  os << "\"bytecodeSources\" : {";

  JsonPrintBytecodeSource(os, -1, info->shared_info()->DebugNameCStr(),
                          info->bytecode_array());

  const auto& inlined = info->inlined_functions();
  SourceIdAssigner id_assigner(info->inlined_functions().size());

  for (unsigned id = 0; id < inlined.size(); id++) {
    Handle<SharedFunctionInfo> shared_info = inlined[id].shared_info;
#if V8_ENABLE_WEBASSEMBLY
    if (shared_info->HasWasmFunctionData()) {
      continue;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    os << ", ";
    const int source_id = id_assigner.GetIdFor(shared_info);
    JsonPrintBytecodeSource(os, source_id, shared_info->DebugNameCStr(),
                            inlined[id].bytecode_array);
  }

  os << "}";
}

void JsonPrintAllSourceWithPositions(std::ostream& os,
                                     OptimizedCompilationInfo* info,
                                     Isolate* isolate) {
  os << "\"sources\" : {";
  Handle<Script> script =
      (info->shared_info().is_null() ||
       info->shared_info()->script() == Tagged<Object>())
          ? Handle<Script>()
          : handle(Cast<Script>(info->shared_info()->script()), isolate);
  JsonPrintFunctionSource(os, -1,
                          info->shared_info().is_null()
                              ? std::unique_ptr<char[]>(new char[1]{0})
                              : info->shared_info()->DebugNameCStr(),
                          script, isolate, info->shared_info(), true);
  const auto& inlined = info->inlined_functions();
  SourceIdAssigner id_assigner(info->inlined_functions().size());
  for (unsigned id = 0; id < inlined.size(); id++) {
    os << ", ";
    Handle<SharedFunctionInfo> shared = inlined[id].shared_info;
    const int source_id = id_assigner.GetIdFor(shared);
    JsonPrintFunctionSource(os, source_id, shared->DebugNameCStr(),
                            handle(Cast<Script>(shared->script()), isolate),
                            isolate, shared, true);
  }
  os << "}, ";
  os << "\"inlinings\" : {";
  bool need_comma = false;
  for (unsigned id = 0; id < inlined.size(); id++) {
    if (need_comma) os << ", ";
    const int source_id = id_assigner.GetIdAt(id);
    JsonPrintInlinedFunctionInfo(os, source_id, id, inlined[id]);
    need_comma = true;
  }
  os << "}";
}

#if V8_ENABLE_WEBASSEMBLY
void JsonPrintAllSourceWithPositionsWasm(
    std::ostream& os, const wasm::WasmModule* module,
    const wasm::WireBytesStorage* wire_bytes,
    base::Vector<WasmInliningPosition> positions) {
  // Filter out duplicate sources. (A single wasm function might be inlined more
  // than once.)
  std::vector<int /*function id*/> sources;
  std::unordered_map<int /*function id*/, size_t /*source index*/> source_map;
  for (WasmInliningPosition pos : positions) {
    auto [_, inserted] =
        source_map.emplace(pos.inlinee_func_index, sources.size());
    if (inserted) {
      // The function wasn't inlined yet. Add a new entry to the sources.
      // The hashmap stores the index to the entry in the source map.
      sources.push_back(pos.inlinee_func_index);
    }
    // Don't do anything if it was already inserted.
  }

  // Print inlining sources.
  os << "\"sources\": {";
  for (size_t i = 0; i < sources.size(); ++i) {
    if (i != 0) os << ", ";
    int function_id = sources[i];
    const wasm::WasmFunction& fct = module->functions[function_id];
    os << '"' << i << "\": {\"sourceId\": " << i << ", \"functionName\": \""
       << fct.func_index << "\", \"sourceName\": \"\", \"sourceText\": \"";
    base::Vector<const uint8_t> module_bytes{nullptr, 0};
    std::optional<wasm::ModuleWireBytes> maybe_wire_bytes =
        wire_bytes->GetModuleBytes();
    if (maybe_wire_bytes) module_bytes = maybe_wire_bytes->module_bytes();
    std::ostringstream wasm_str;
    wasm::DisassembleFunction(module, function_id,
                              wire_bytes->GetCode(fct.code), module_bytes,
                              fct.code.offset(), wasm_str);
    os << JSONEscaped(wasm_str) << "\"}";
  }
  os << "},\n";
  // Print inlining mappings.
  // This maps the inlining position to the deduplicated source in the sources
  // object generated above.
  os << "\"inlinings\": {";
  for (size_t i = 0; i < positions.size(); ++i) {
    if (i != 0) os << ", ";
    DCHECK(source_map.contains(positions[i].inlinee_func_index));
    size_t source_id = source_map.find(positions[i].inlinee_func_index)->second;
    SourcePosition inlining_pos = positions[i].caller_pos;
    os << '"' << i << "\": {\"inliningId\": " << i
       << ", \"sourceId\": " << source_id
       << ", \"inliningPosition\": " << AsJSON(inlining_pos) << "}";
  }
}
#endif

std::unique_ptr<char[]> GetVisualizerLogFileName(OptimizedCompilationInfo* info,
                                                 const char* optional_base_dir,
                                                 const char* phase,
                                                 const char* suffix) {
  base::EmbeddedVector<char, 256> filename(0);
  std::unique_ptr<char[]> debug_name = info->GetDebugName();
  const char* file_prefix = v8_flags.trace_turbo_file_prefix.value();
  int optimization_id = info->IsOptimizing() ? info->optimization_id() : 0;
  if (strlen(debug_name.get()) > 0) {
    if (strcmp(debug_name.get(), "WasmJSFastApiCall") == 0) {
      // Don't clobber one wrapper's output with another's.
      static int fast_call_wrappers_count = 0;
      optimization_id = ++fast_call_wrappers_count;
    }
    SNPrintF(filename, "%s-%s-%i", file_prefix, debug_name.get(),
             optimization_id);
  } else if (info->has_shared_info()) {
    SNPrintF(filename,  "%s-%p-%i", file_prefix,
            reinterpret_cast<void*>(info->shared_info()->address()),
            optimization_id);
  } else {
    SNPrintF(filename, "%s-none-%i", file_prefix, optimization_id);
  }
  base::EmbeddedVector<char, 256> source_file(0);
  bool source_available = false;
  if (v8_flags.trace_file_names && info->has_shared_info() &&
      IsScript(info->shared_info()->script())) {
    Tagged<Object> source_name =
        Cast<Script>(info->shared_info()->script())->name();
    if (IsString(source_name)) {
      Tagged<String> str = Cast<String>(source_name);
      if (str->length() > 0) {
        SNPrintF(source_file, "%s", str->ToCString().get());
        std::replace(source_file.begin(),
                     source_file.begin() + source_file.length(), '/', '_');
        source_available = true;
      }
    }
  }
  std::replace(filename.begin(), filename.begin() + filename.length(), ' ',
               '_');
  std::replace(filename.begin(), filename.begin() + filename.length(), ':',
               '-');

  base::EmbeddedVector<char, 256> base_dir;
  if (optional_base_dir != nullptr) {
    SNPrintF(base_dir, "%s%c", optional_base_dir,
             base::OS::DirectorySeparator());
  } else {
    base_dir[0] = '\0';
  }

  base::EmbeddedVector<char, 256> full_filename;
  if (phase == nullptr && !source_available) {
    SNPrintF(full_filename, "%s%s.%s", base_dir.begin(), filename.begin(),
             suffix);
  } else if (phase != nullptr && !source_available) {
    SNPrintF(full_filename, "%s%s-%s.%s", base_dir.begin(), filename.begin(),
             phase, suffix);
  } else if (phase == nullptr && source_available) {
    SNPrintF(full_filename, "%s%s_%s.%s", base_dir.begin(), filename.begin(),
             source_file.begin(), suffix);
  } else {
    SNPrintF(full_filename, "%s%s_%s-%s.%s", base_dir.begin(), filename.begin(),
             source_file.begin(), phase, suffix);
  }

  char* buffer = new char[full_filename.length() + 1];
  memcpy(buffer, full_filename.begin(), full_filename.length());
  buffer[full_filename.length()] = '\0';
  return std::unique_ptr<char[]>(buffer);
}


static int SafeId(Node* node) { return node == nullptr ? -1 : node->id(); }
static const char* SafeMnemonic(Node* node) {
  return node == nullptr ? "null" : node->op()->mnemonic();
}

JSONGraphWriter::JSONGraphWriter(std::ostream& os, const Graph* graph,
                                 const SourcePositionTable* positions,
                                 const NodeOriginTable* origins)
    : os_(os),
      zone_(nullptr),
      graph_(graph),
      positions_(positions),
      origins_(origins),
      first_node_(true),
      first_edge_(true) {}

void JSONGraphWriter::PrintPhase(const char* phase_name) {
  os_ << "{\"name\":\"" << phase_name << "\",\"type\":\"graph\",\"data\":";
  Print();
  os_ << "},\n";
}

void JSONGraphWriter::Print() {
  AccountingAllocator allocator;
  Zone tmp_zone(&allocator, ZONE_NAME);
  zone_ = &tmp_zone;

  AllNodes all(zone_, graph_, false);
  AllNodes live(zone_, graph_, true);

  os_ << "{\n\"nodes\":[";
  for (Node* const node : all.reachable) PrintNode(node, live.IsLive(node));
  os_ << "\n";
  os_ << "],\n\"edges\":[";
  for (Node* const node : all.reachable) PrintEdges(node);
  os_ << "\n";
  os_ << "]}";
  zone_ = nullptr;
}

void JSONGraphWriter::PrintNode(Node* node, bool is_live) {
  if (first_node_) {
    first_node_ = false;
  } else {
    os_ << ",\n";
  }
  std::ostringstream label, title, properties;
  node->op()->PrintTo(label, Operator::PrintVerbosity::kSilent);
  node->op()->PrintTo(title, Operator::PrintVerbosity::kVerbose);
  node->op()->PrintPropsTo(properties);
  os_ << "{\"id\":" << SafeId(node) << ",\"label\":\"" << JSONEscaped(label)
      << "\""
      << ",\"title\":\"" << JSONEscaped(title) << "\""
      << ",\"live\": " << (is_live ? "true" : "false") << ",\"properties\":\""
      << JSONEscaped(properties) << "\"";
  IrOpcode::Value opcode = node->opcode();
  if (IrOpcode::IsPhiOpcode(opcode)) {
    os_ << ",\"rankInputs\":[0," << NodeProperties::FirstControlIndex(node)
        << "]";
    os_ << ",\"rankWithInput\":[" << NodeProperties::FirstControlIndex(node)
        << "]";
  } else if (opcode == IrOpcode::kIfTrue || opcode == IrOpcode::kIfFalse ||
             opcode == IrOpcode::kLoop) {
    os_ << ",\"rankInputs\":[" << NodeProperties::FirstControlIndex(node)
        << "]";
  }
  if (opcode == IrOpcode::kBranch) {
    os_ << ",\"rankInputs\":[0]";
  }
  if (positions_ != nullptr) {
    SourcePosition position = positions_->GetSourcePosition(node);
    if (position.IsKnown()) {
      os_ << ", \"sourcePosition\" : " << AsJSON(position);
    }
  }
  if (origins_) {
    NodeOrigin origin = origins_->GetNodeOrigin(node);
    if (origin.IsKnown()) {
      os_ << ", \"origin\" : " << AsJSON(origin);
    }
  }
  os_ << ",\"opcode\":\"" << IrOpcode::Mnemonic(node->opcode()) << "\"";
  os_ << ",\"control\":"
      << (NodeProperties::IsControl(node) ? "true" : "false");
  os_ << ",\"opinfo\":\"" << node->op()->ValueInputCount() << " v "
      << node->op()->EffectInputCount() << " eff "
      << node->op()->ControlInputCount() << " ctrl in, "
      << node->op()->ValueOutputCount() << " v "
      << node->op()->EffectOutputCount() << " eff "
      << node->op()->ControlOutputCount() << " ctrl out\"";
  if (auto type_opt = GetType(node)) {
    std::ostringstream type_out;
    type_opt->PrintTo(type_out);
    os_ << ",\"type\":\"" << JSONEscaped(type_out) << "\"";
  }
  os_ << "}";
}

void JSONGraphWriter::PrintEdges(Node* node) {
  for (int i = 0; i < node->InputCount(); i++) {
    Node* input = node->InputAt(i);
    if (input == nullptr) continue;
    PrintEdge(node, i, input);
  }
}

void JSONGraphWriter::PrintEdge(Node* from, int index, Node* to) {
  if (first_edge_) {
    first_edge_ = false;
  } else {
    os_ << ",\n";
  }
  const char* edge_type = nullptr;
  if (index < NodeProperties::FirstValueIndex(from)) {
    edge_type = "unknown";
  } else if (index < NodeProperties::FirstContextIndex(from)) {
    edge_type = "value";
  } else if (index < NodeProperties::FirstFrameStateIndex(from)) {
    edge_type = "context";
  } else if (index < NodeProperties::FirstEffectIndex(from)) {
    edge_type = "frame-state";
  } else if (index < NodeProperties::FirstControlIndex(from)) {
    edge_type = "effect";
  } else {
    edge_type = "control";
  }
  os_ << "{\"source\":" << SafeId(to) << ",\"target\":" << SafeId(from)
      << ",\"index\":" << index << ",\"type\":\"" << edge_type << "\"}";
}

std::optional<Type> JSONGraphWriter::GetType(Node* node) {
  if (!NodeProperties::IsTyped(node)) return std::nullopt;
  return NodeProperties::GetType(node);
}

std::ostream& operator<<(std::ostream& os, const GraphAsJSON& ad) {
  JSONGraphWriter writer(os, &ad.graph, ad.positions, ad.origins);
  writer.Print();
  return os;
}


class GraphC1Visualizer {
 public:
  GraphC1Visualizer(std::ostream& os, Zone* zone);
  GraphC1Visualizer(const GraphC1Visualizer&) = delete;
  GraphC1Visualizer& operator=(const GraphC1Visualizer&) = delete;

  void PrintCompilation(const OptimizedCompilationInfo* info);
  void PrintSchedule(const char* phase, const Schedule* schedule,
                     const SourcePositionTable* positions,
                     const InstructionSequence* instructions);
  void PrintLiveRanges(const char* phase, const RegisterAllocationData* data);
  Zone* zone() const { return zone_; }

 private:
  void PrintIndent();
  void PrintStringProperty(const char* name, const char* value);
  void PrintLongProperty(const char* name, int64_t value);
  void PrintIntProperty(const char* name, int value);
  void PrintBlockProperty(const char* name, int rpo_number);
  void PrintNodeId(Node* n);
  void PrintNode(Node* n);
  void PrintInputs(Node* n);
  template <typename InputIterator>
  void PrintInputs(InputIterator* i, int count, const char* prefix);
  void PrintType(Node* node);

  void PrintLiveRange(const LiveRange* range, const char* type, int vreg);
  void PrintLiveRangeChain(const TopLevelLiveRange* range, const char* type);

  class Tag final {
   public:
    Tag(GraphC1Visualizer* visualizer, const char* name) {
      name_ = name;
      visualizer_ = visualizer;
      visualizer->PrintIndent();
      visualizer_->os_ << "begin_" << name << "\n";
      visualizer->indent_++;
    }

    ~Tag() {
      visualizer_->indent_--;
      visualizer_->PrintIndent();
      visualizer_->os_ << "end_" << name_ << "\n";
      DCHECK_LE(0, visualizer_->indent_);
    }

   private:
    GraphC1Visualizer* visualizer_;
    const char* name_;
  };

  std::ostream& os_;
  int indent_;
  Zone* zone_;
};


void GraphC1Visualizer::PrintIndent() {
  for (int i = 0; i < indent_; i++) {
    os_ << "  ";
  }
}


GraphC1Visualizer::GraphC1Visualizer(std::ostream& os, Zone* zone)
    : os_(os), indent_(0), zone_(zone) {}


void GraphC1Visualizer::PrintStringProperty(const char* name,
                                            const char* value) {
  PrintIndent();
  os_ << name << " \"" << value << "\"\n";
}


void GraphC1Visualizer::PrintLongProperty(const char* name, int64_t value) {
  PrintIndent();
  os_ << name << " " << static_cast<int>(value / 1000) << "\n";
}


void GraphC1Visualizer::PrintBlockProperty(const char* name, int rpo_number) {
  PrintIndent();
  os_ << name << " \"B" << rpo_number << "\"\n";
}


void GraphC1Visualizer::PrintIntProperty(const char* name, int value) {
  PrintIndent();
  os_ << name << " " << value << "\n";
}

void GraphC1Visualizer::PrintCompilation(const OptimizedCompilationInfo* info) {
  Tag tag(this, "compilation");
  std::unique_ptr<char[]> name = info->GetDebugName();
  if (info->IsOptimizing()) {
    PrintStringProperty("name", name.get());
    PrintIndent();
    os_ << "method \"" << name.get() << ":" << info->optimization_id()
        << "\"\n";
  } else {
    PrintStringProperty("name", name.get());
    PrintStringProperty("method", "stub");
  }
  PrintLongProperty("date",
                    V8::GetCurrentPlatform()->CurrentClockTimeMilliseconds());
}


void GraphC1Visualizer::PrintNodeId(Node* n) { os_ << "n" << SafeId(n); }


void GraphC1Visualizer::PrintNode(Node* n) {
  PrintNodeId(n);
  os_ << " " << *n->op() << " ";
  PrintInputs(n);
}


template <typename InputIterator>
void GraphC1Visualizer::PrintInputs(InputIterator* i, int count,
                                    const char* prefix) {
  if (count > 0) {
    os_ << prefix;
  }
  while (count > 0) {
    os_ << " ";
    PrintNodeId(**i);
    ++(*i);
    count--;
  }
}


void GraphC1Visualizer::PrintInputs(Node* node) {
  auto i = node->inputs().begin();
  PrintInputs(&i, node->op()->ValueInputCount(), " ");
  PrintInputs(&i, OperatorProperties::GetContextInputCount(node->op()),
              " Ctx:");
  PrintInputs(&i, OperatorProperties::GetFrameStateInputCount(node->op()),
              " FS:");
  PrintInputs(&i, node->op()->EffectInputCount(), " Eff:");
  PrintInputs(&i, node->op()->ControlInputCount(), " Ctrl:");
}


void GraphC1Visualizer::PrintType(Node* node) {
  if (NodeProperties::IsTyped(node)) {
    Type type = NodeProperties::GetType(node);
    os_ << " type:" << type;
  }
}


void GraphC1Visualizer::PrintSchedule(const char* phase,
                                      const Schedule* schedule,
                                      const SourcePositionTable* positions,
                                      const InstructionSequence* instructions) {
  Tag tag(this, "cfg");
  PrintStringProperty("name", phase);
  const BasicBlockVector* rpo = schedule->rpo_order();
  for (size_t i = 0; i < rpo->size(); i++) {
    BasicBlock* current = (*rpo)[i];
    Tag block_tag(this, "block");
    PrintBlockProperty("name", current->rpo_number());
    PrintIntProperty("from_bci", -1);
    PrintIntProperty("to_bci", -1);

    PrintIndent();
    os_ << "predecessors";
    for (BasicBlock* predecessor : current->predecessors()) {
      os_ << " \"B" << predecessor->rpo_number() << "\"";
    }
    os_ << "\n";

    PrintIndent();
    os_ << "successors";
    for (BasicBlock* successor : current->successors()) {
      os_ << " \"B" << successor->rpo_number() << "\"";
    }
    os_ << "\n";

    PrintIndent();
    os_ << "xhandlers\n";

    PrintIndent();
    os_ << "flags\n";

    if (current->dominator() != nullptr) {
      PrintBlockProperty("dominator", current->dominator()->rpo_number());
    }

    PrintIntProperty("loop_depth", current->loop_depth());

    const InstructionBlock* instruction_block =
        instructions->InstructionBlockAt(
            RpoNumber::FromInt(current->rpo_number()));
    if (instruction_block->code_start() >= 0) {
      int first_index = instruction_block->first_instruction_index();
      int last_index = instruction_block->last_instruction_index();
      PrintIntProperty(
          "first_lir_id",
          LifetimePosition::GapFromInstructionIndex(first_index).value());
      PrintIntProperty("last_lir_id",
                       LifetimePosition::InstructionFromInstructionIndex(
                           last_index).value());
    }

    {
      Tag states_tag(this, "states");
      Tag locals_tag(this, "locals");
      int total = 0;
      for (BasicBlock::const_iterator it = current->begin();
           it != current->end(); ++it) {
        if ((*it)->opcode() == IrOpcode::kPhi) total++;
      }
      PrintIntProperty("size", total);
      PrintStringProperty("method", "None");
      int index = 0;
      for (BasicBlock::const_iterator it = current->begin();
           it != current->end(); ++it) {
        if ((*it)->opcode() != IrOpcode::kPhi) continue;
        PrintIndent();
        os_ << index << " ";
        PrintNodeId(*it);
        os_ << " [";
        PrintInputs(*it);
        os_ << "]\n";
        index++;
      }
    }

    {
      Tag HIR_tag(this, "HIR");
      for (BasicBlock::const_iterator it = current->begin();
           it != current->end(); ++it) {
        Node* node = *it;
        if (node->opcode() == IrOpcode::kPhi) continue;
        int uses = node->UseCount();
        PrintIndent();
        os_ << "0 " << uses << " ";
        PrintNode(node);
        if (v8_flags.trace_turbo_types) {
          os_ << " ";
          PrintType(node);
        }
        if (positions != nullptr) {
          SourcePosition position = positions->GetSourcePosition(node);
          if (position.IsKnown()) {
            os_ << " pos:";
            if (position.isInlined()) {
              os_ << "inlining(" << position.InliningId() << "),";
            }
            os_ << position.ScriptOffset();
          }
        }
        os_ << " <|@\n";
      }

      BasicBlock::Control control = current->control();
      if (control != BasicBlock::kNone) {
        PrintIndent();
        os_ << "0 0 ";
        if (current->control_input() != nullptr) {
          PrintNode(current->control_input());
        } else {
          os_ << -1 - current->rpo_number() << " Goto";
        }
        os_ << " ->";
        for (BasicBlock* successor : current->successors()) {
          os_ << " B" << successor->rpo_number();
        }
        if (v8_flags.trace_turbo_types && current->control_input() != nullptr) {
          os_ << " ";
          PrintType(current->control_input());
        }
        os_ << " <|@\n";
      }
    }

    if (instructions != nullptr) {
      Tag LIR_tag(this, "LIR");
      for (int j = instruction_block->first_instruction_index();
           j <= instruction_block->last_instruction_index(); j++) {
        PrintIndent();
        os_ << j << " " << *instructions->InstructionAt(j) << " <|@\n";
      }
    }
  }
}

void GraphC1Visualizer::PrintLiveRanges(const char* phase,
                                        const RegisterAllocationData* data) {
  Tag tag(this, "intervals");
  PrintStringProperty("name", phase);

  for (const TopLevelLiveRange* range : data->fixed_double_live_ranges()) {
    PrintLiveRangeChain(range, "fixed");
  }

  for (const TopLevelLiveRange* range : data->fixed_live_ranges()) {
    PrintLiveRangeChain(range, "fixed");
  }

  for (const TopLevelLiveRange* range : data->live_ranges()) {
    PrintLiveRangeChain(range, "object");
  }
}

void GraphC1Visualizer::PrintLiveRangeChain(const TopLevelLiveRange* range,
                                            const char* type) {
  if (range == nullptr || range->IsEmpty()) return;
  int vreg = range->vreg();
  for (const LiveRange* child = range; child != nullptr;
       child = child->next()) {
    PrintLiveRange(child, type, vreg);
  }
}

void GraphC1Visualizer::PrintLiveRange(const LiveRange* range, const char* type,
                                       int vreg) {
  if (range != nullptr && !range->IsEmpty()) {
    PrintIndent();
    os_ << vreg << ":" << range->relative_id() << " " << type;
    if (range->HasRegisterAssigned()) {
      AllocatedOperand op = AllocatedOperand::cast(range->GetAssignedOperand());
      if (op.IsRegister()) {
        os_ << " \"" << Register::from_code(op.register_code()) << "\"";
      } else if (op.IsDoubleRegister()) {
        os_ << " \"" << DoubleRegister::from_code(op.register_code()) << "\"";
      } else if (op.IsFloatRegister()) {
        os_ << " \"" << FloatRegister::from_code(op.register_code()) << "\"";
#if defined(V8_TARGET_ARCH_X64)
      } else if (op.IsSimd256Register()) {
        os_ << " \"" << Simd256Register::from_code(op.register_code()) << "\"";
#endif
      } else {
        DCHECK(op.IsSimd128Register());
        os_ << " \"" << Simd128Register::from_code(op.register_code()) << "\"";
      }
    } else if (range->spilled()) {
      const TopLevelLiveRange* top = range->TopLevel();
      int index = -1;
      if (top->HasSpillRange()) {
        index = kMaxInt;  // This hasn't been set yet.
      } else if (top->GetSpillOperand()->IsConstant()) {
        os_ << " \"const(nostack):"
            << ConstantOperand::cast(top->GetSpillOperand())->virtual_register()
            << "\"";
      } else {
        index = AllocatedOperand::cast(top->GetSpillOperand())->index();
        if (IsFloatingPoint(top->representation())) {
          os_ << " \"fp_stack:" << index << "\"";
        } else {
          os_ << " \"stack:" << index << "\"";
        }
      }
    }

    const TopLevelLiveRange* parent = range->TopLevel();
    os_ << " " << parent->vreg() << ":" << parent->relative_id();

    // TODO(herhut) Find something useful to print for the hint field
    if (parent->get_bundle() != nullptr) {
      os_ << " B" << parent->get_bundle()->id();
    } else {
      os_ << " unknown";
    }

    for (const UseInterval& interval : range->intervals()) {
      os_ << " [" << interval.start().value() << ", " << interval.end().value()
          << "[";
    }

    for (const UsePosition* pos : range->positions()) {
      if (pos->RegisterIsBeneficial() || v8_flags.trace_all_uses) {
        os_ << " " << pos->pos().value() << " M";
      }
    }

    os_ << " \"\"\n";
  }
}


std::ostream& operator<<(std::ostream& os, const AsC1VCompilation& ac) {
  AccountingAllocator allocator;
  Zone tmp_zone(&allocator, ZONE_NAME);
  GraphC1Visualizer(os, &tmp_zone).PrintCompilation(ac.info_);
  return os;
}


std::ostream& operator<<(std::ostream& os, const AsC1V& ac) {
  AccountingAllocator allocator;
  Zone tmp_zone(&allocator, ZONE_NAME);
  GraphC1Visualizer(os, &tmp_zone)
      .PrintSchedule(ac.phase_, ac.schedule_, ac.positions_, ac.instructions_);
  return os;
}


std::ostream& operator<<(std::ostream& os,
                         const AsC1VRegisterAllocationData& ac) {
  AccountingAllocator allocator;
  Zone tmp_zone(&allocator, ZONE_NAME);
  GraphC1Visualizer(os, &tmp_zone).PrintLiveRanges(ac.phase_, ac.data_);
  return os;
}

const int kUnvisited = 0;
const int kOnStack = 1;
const int kVisited = 2;

std::ostream& operator<<(std::ostream& os, const AsRPO& ar) {
  AccountingAllocator allocator;
  Zone local_zone(&allocator, ZONE_NAME);

  // Do a post-order depth-first search on the RPO graph. For every node,
  // print:
  //
  //   - the node id
  //   - the operator mnemonic
  //   - in square brackets its parameter (if present)
  //   - in parentheses the list of argument ids and their mnemonics
  //   - the node type (if it is typed)

  // Post-order guarantees that all inputs of a node will be printed before
  // the node itself, if there are no cycles. Any cycles are broken
  // arbitrarily.

  ZoneVector<uint8_t> state(ar.graph.NodeCount(), kUnvisited, &local_zone);
  ZoneStack<Node*> stack(&local_zone);

  stack.push(ar.graph.end());
  state[ar.graph.end()->id()] = kOnStack;
  while (!stack.empty()) {
    Node* n = stack.top();
    bool pop = true;
    for (Node* const i : n->inputs()) {
      if (state[i->id()] == kUnvisited) {
        state[i->id()] = kOnStack;
        stack.push(i);
        pop = false;
        break;
      }
    }
    if (pop) {
      state[n->id()] = kVisited;
      stack.pop();
      os << "#" << n->id() << ":" << *n->op() << "(";
      // Print the inputs.
      int j = 0;
      for (Node* const i : n->inputs()) {
        if (j++ > 0) os << ", ";
        os << "#" << SafeId(i) << ":" << SafeMnemonic(i);
      }
      os << ")";
      // Print the node type, if any.
      if (NodeProperties::IsTyped(n)) {
        os << "  [Type: " << NodeProperties::GetType(n) << "]";
      }
      os << std::endl;
    }
  }
  return os;
}

namespace {

void PrintIndent(std::ostream& os, int indent) {
  os << "     ";
  for (int i = 0; i < indent; i++) {
    os << ". ";
  }
}

void PrintScheduledNode(std::ostream& os, int indent, Node* n) {
  PrintIndent(os, indent);
  os << "#" << n->id() << ":" << *n->op() << "(";
  // Print the inputs.
  int j = 0;
  for (Node* const i : n->inputs()) {
    if (j++ > 0) os << ", ";
    os << "#" << SafeId(i) << ":" << SafeMnemonic(i);
  }
  os << ")";
  // Print the node type, if any.
  if (NodeProperties::IsTyped(n)) {
    os << "  [Type: " << NodeProperties::GetType(n) << "]";
  }
}

void PrintScheduledGraph(std::ostream& os, const Schedule* schedule) {
  const BasicBlockVector* rpo = schedule->rpo_order();
  for (size_t i = 0; i < rpo->size(); i++) {
    BasicBlock* current = (*rpo)[i];
    int indent = current->loop_depth();

    os << "  + Block B" << current->rpo_number() << " (pred:";
    for (BasicBlock* predecessor : current->predecessors()) {
      os << " B" << predecessor->rpo_number();
    }
    if (current->IsLoopHeader()) {
      os << ", loop until B" << current->loop_end()->rpo_number();
    } else if (current->loop_header()) {
      os << ", in loop B" << current->loop_header()->rpo_number();
    }
    os << ")" << std::endl;

    for (BasicBlock::const_iterator it = current->begin(); it != current->end();
         ++it) {
      Node* node = *it;
      PrintScheduledNode(os, indent, node);
      os << std::endl;
    }

    if (current->SuccessorCount() > 0) {
      if (current->control_input() != nullptr) {
        PrintScheduledNode(os, indent, current->control_input());
      } else {
        PrintIndent(os, indent);
        os << "Goto";
      }
      os << " ->";

      bool isFirst = true;
      for (BasicBlock* successor : current->successors()) {
        if (isFirst) {
          isFirst = false;
        } else {
          os << ",";
        }
        os << " B" << successor->rpo_number();
      }
      os << std::endl;
    } else {
      DCHECK_NULL(current->control_input());
    }
  }
}

}  // namespace

std::ostream& operator<<(std::ostream& os,
                         const LiveRangeAsJSON& live_range_json) {
  const LiveRange& range = live_range_json.range_;
  os << "{\"id\":" << range.relative_id() << ",\"type\":";
  if (range.HasRegisterAssigned()) {
    const InstructionOperand op = range.GetAssignedOperand();
    os << "\"assigned\",\"op\":"
       << InstructionOperandAsJSON{&op, &(live_range_json.code_)};
  } else if (range.spilled() && !range.TopLevel()->HasNoSpillType()) {
    const TopLevelLiveRange* top = range.TopLevel();
    if (top->HasSpillOperand()) {
      os << "\"assigned\",\"op\":"
         << InstructionOperandAsJSON{top->GetSpillOperand(),
                                     &(live_range_json.code_)};
    } else {
      int index = top->GetSpillRange()->assigned_slot();
      os << "\"spilled\",\"op\":";
      if (IsFloatingPoint(top->representation())) {
        os << "\"fp_stack:" << index << "\"";
      } else {
        os << "\"stack:" << index << "\"";
      }
    }
  } else {
    os << "\"none\"";
  }

  os << ",\"intervals\":[";
  bool first = true;
  for (const UseInterval& interval : range.intervals()) {
    if (first) {
      first = false;
    } else {
      os << ",";
    }
    os << "[" << interval.start().value() << "," << interval.end().value()
       << "]";
  }

  os << "],\"uses\":[";
  first = true;
  for (const UsePosition* pos : range.positions()) {
    if (first) {
      first = false;
    } else {
      os << ",";
    }
    os << pos->pos().value();
  }

  os << "]}";
  return os;
}

std::ostream& operator<<(
    std::ostream& os,
    const TopLevelLiveRangeAsJSON& top_level_live_range_json) {
  int vreg = top_level_live_range_json.range_.vreg();
  bool first = true;
  int instruction_range[2] = {INT32_MAX, -1};
  os << "\"" << (vreg > 0 ? vreg : -vreg) << "\":{ \"child_ranges\":[";
  for (const LiveRange* child = &(top_level_live_range_json.range_);
       child != nullptr; child = child->next()) {
    if (!top_level_live_range_json.range_.IsEmpty()) {
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << LiveRangeAsJSON{*child, top_level_live_range_json.code_};
      // Record the minimum and maximum positions observed within this
      // TopLevelLiveRange
      for (const UseInterval& interval : child->intervals()) {
        if (interval.start().value() < instruction_range[0])
          instruction_range[0] = interval.start().value();
        if (interval.end().value() > instruction_range[1])
          instruction_range[1] = interval.end().value();
      }
    }
  }
  os << "]";
  if (top_level_live_range_json.range_.IsFixed()) {
    os << ", \"is_deferred\": "
       << (top_level_live_range_json.range_.IsDeferredFixed() ? "true"
                                                              : "false");
  }
  os << ", \"instruction_range\": [" << instruction_range[0] << ","
     << instruction_range[1] << "]}";
  return os;
}

void PrintTopLevelLiveRanges(std::ostream& os,
                             const ZoneVector<TopLevelLiveRange*> ranges,
                             const InstructionSequence& code) {
  bool first = true;
  os << "{";
  for (const TopLevelLiveRange* range : ranges) {
    if (range != nullptr && !range->IsEmpty()) {
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << TopLevelLiveRangeAsJSON{*range, code};
    }
  }
  os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const RegisterAllocationDataAsJSON& ac) {
  os << "\"fixed_double_live_ranges\": ";
  PrintTopLevelLiveRanges(os, ac.data_.fixed_double_live_ranges(), ac.code_);
  os << ",\"fixed_live_ranges\": ";
  PrintTopLevelLiveRanges(os, ac.data_.fixed_live_ranges(), ac.code_);
  os << ",\"live_ranges\": ";
  PrintTopLevelLiveRanges(os, ac.data_.live_ranges(), ac.code_);
  return os;
}

std::ostream& operator<<(std::ostream& os, const AsScheduledGraph& scheduled) {
  PrintScheduledGraph(os, scheduled.schedule);
  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionOperandAsJSON& o) {
  const InstructionOperand* op = o.op_;
  const InstructionSequence* code = o.code_;
  os << "{";
  switch (op->kind()) {
    case InstructionOperand::UNALLOCATED: {
      const UnallocatedOperand* unalloc = UnallocatedOperand::cast(op);
      os << "\"type\": \"unallocated\", ";
      os << "\"text\": \"v" << unalloc->virtual_register() << "\"";
      if (unalloc->basic_policy() == UnallocatedOperand::FIXED_SLOT) {
        os << ",\"tooltip\": \"FIXED_SLOT: " << unalloc->fixed_slot_index()
           << "\"";
        break;
      }
      switch (unalloc->extended_policy()) {
        case UnallocatedOperand::NONE:
          break;
        case UnallocatedOperand::FIXED_REGISTER: {
          os << ",\"tooltip\": \"FIXED_REGISTER: "
             << Register::from_code(unalloc->fixed_register_index()) << "\"";
          break;
        }
        case UnallocatedOperand::FIXED_FP_REGISTER: {
          os << ",\"tooltip\": \"FIXED_FP_REGISTER: "
             << DoubleRegister::from_code(unalloc->fixed_register_index())
             << "\"";
          break;
        }
        case UnallocatedOperand::MUST_HAVE_REGISTER: {
          os << ",\"tooltip\": \"MUST_HAVE_REGISTER\"";
          break;
        }
        case UnallocatedOperand::MUST_HAVE_SLOT: {
          os << ",\"tooltip\": \"MUST_HAVE_SLOT\"";
          break;
        }
        case UnallocatedOperand::SAME_AS_INPUT: {
          os << ",\"tooltip\": \"SAME_AS_INPUT: " << unalloc->input_index()
             << "\"";
          break;
        }
        case UnallocatedOperand::REGISTER_OR_SLOT: {
          os << ",\"tooltip\": \"REGISTER_OR_SLOT\"";
          break;
        }
        case UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT: {
          os << ",\"tooltip\": \"REGISTER_OR_SLOT_OR_CONSTANT\"";
          break;
        }
      }
      break;
    }
    case InstructionOperand::CONSTANT: {
      int vreg = ConstantOperand::cast(op)->virtual_register();
      os << "\"type\": \"constant\", ";
      os << "\"text\": \"v" << vreg << "\",";
      os << "\"tooltip\": \"";
      std::stringstream tooltip;
      tooltip << code->GetConstant(vreg);
      for (const auto& c : tooltip.str()) {
        os << AsEscapedUC16ForJSON(c);
      }
      os << "\"";
      break;
    }
    case InstructionOperand::IMMEDIATE: {
      os << "\"type\": \"immediate\", ";
      const ImmediateOperand* imm = ImmediateOperand::cast(op);
      switch (imm->type()) {
        case ImmediateOperand::INLINE_INT32: {
          os << "\"text\": \"#" << imm->inline_int32_value() << "\"";
          break;
        }
        case ImmediateOperand::INLINE_INT64: {
          os << "\"text\": \"#" << imm->inline_int64_value() << "\"";
          break;
        }
        case ImmediateOperand::INDEXED_RPO:
        case ImmediateOperand::INDEXED_IMM: {
          int index = imm->indexed_value();
          os << "\"text\": \"imm:" << index << "\",";
          os << "\"tooltip\": \"";
          std::stringstream tooltip;
          tooltip << code->GetImmediate(imm);
          for (const auto& c : tooltip.str()) {
            os << AsEscapedUC16ForJSON(c);
          }
          os << "\"";
          break;
        }
      }
      break;
    }
    case InstructionOperand::ALLOCATED: {
      const LocationOperand* allocated = LocationOperand::cast(op);
      os << "\"type\": \"allocated\", ";
      os << "\"text\": \"";
      if (op->IsStackSlot()) {
        os << "stack:" << allocated->index();
      } else if (op->IsFPStackSlot()) {
        os << "fp_stack:" << allocated->index();
      } else if (op->IsRegister()) {
        if (allocated->register_code() < Register::kNumRegisters) {
          os << Register::from_code(allocated->register_code());
        } else {
          os << Register::GetSpecialRegisterName(allocated->register_code());
        }
      } else if (op->IsDoubleRegister()) {
        os << DoubleRegister::from_code(allocated->register_code());
      } else if (op->IsFloatRegister()) {
        os << FloatRegister::from_code(allocated->register_code());
#if defined(V8_TARGET_ARCH_X64)
      } else if (op->IsSimd256Register()) {
        os << Simd256Register::from_code(allocated->register_code());
#endif
      } else {
        DCHECK(op->IsSimd128Register());
        os << Simd128Register::from_code(allocated->register_code());
      }
      os << "\",";
      os << "\"tooltip\": \""
         << MachineReprToString(allocated->representation()) << "\"";
      break;
    }
    case InstructionOperand::PENDING:
    case InstructionOperand::INVALID:
      UNREACHABLE();
  }
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionAsJSON& i_json) {
  const Instruction* instr = i_json.instr_;

  os << "{";
  os << "\"id\": " << i_json.index_ << ",";
  os << "\"opcode\": \"" << ArchOpcodeField::decode(instr->opcode()) << "\",";
  os << "\"flags\": \"";
  FlagsMode fm = FlagsModeField::decode(instr->opcode());
  AddressingMode am = AddressingModeField::decode(instr->opcode());
  if (am != kMode_None) {
    os << " : " << AddressingModeField::decode(instr->opcode());
  }
  if (fm != kFlags_none) {
    os << " && " << fm << " if "
       << FlagsConditionField::decode(instr->opcode());
  }
  os << "\",";

  os << "\"gaps\": [";
  for (int i = Instruction::FIRST_GAP_POSITION;
       i <= Instruction::LAST_GAP_POSITION; i++) {
    if (i != Instruction::FIRST_GAP_POSITION) os << ",";
    os << "[";
    const ParallelMove* pm = instr->parallel_moves()[i];
    if (pm == nullptr) {
      os << "]";
      continue;
    }
    bool first = true;
    for (MoveOperands* move : *pm) {
      if (move->IsEliminated()) continue;
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << "[" << InstructionOperandAsJSON{&move->destination(), i_json.code_}
         << "," << InstructionOperandAsJSON{&move->source(), i_json.code_}
         << "]";
    }
    os << "]";
  }
  os << "],";

  os << "\"outputs\": [";
  bool need_comma = false;
  for (size_t i = 0; i < instr->OutputCount(); i++) {
    if (need_comma) os << ",";
    need_comma = true;
    os << InstructionOperandAsJSON{instr->OutputAt(i), i_json.code_};
  }
  os << "],";

  os << "\"inputs\": [";
  need_comma = false;
  for (size_t i = 0; i < instr->InputCount(); i++) {
    if (need_comma) os << ",";
    need_comma = true;
    os << InstructionOperandAsJSON{instr->InputAt(i), i_json.code_};
  }
  os << "],";

  os << "\"temps\": [";
  need_comma = false;
  for (size_t i = 0; i < instr->TempCount(); i++) {
    if (need_comma) os << ",";
    need_comma = true;
    os << InstructionOperandAsJSON{instr->TempAt(i), i_json.code_};
  }
  os << "]";
  os << "}";

  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionBlockAsJSON& b) {
  const InstructionBlock* block = b.block_;
  const InstructionSequence* code = b.code_;
  os << "{";
  os << "\"id\": " << block->rpo_number() << ",";
  os << "\"deferred\": " << (block->IsDeferred() ? "true" : "false");
  os << ",";
  os << "\"loop_header\": " << block->IsLoopHeader() << ",";
  if (block->IsLoopHeader()) {
    os << "\"loop_end\": " << block->loop_end() << ",";
  }
  os << "\"predecessors\": [";
  bool need_comma = false;
  for (RpoNumber pred : block->predecessors()) {
    if (need_comma) os << ",";
    need_comma = true;
    os << pred.ToInt();
  }
  os << "],";
  os << "\"successors\": [";
  need_comma = false;
  for (RpoNumber succ : block->successors()) {
    if (need_comma) os << ",";
    need_comma = true;
    os << succ.ToInt();
  }
  os << "],";
  os << "\"phis\": [";
  bool needs_comma = false;
  InstructionOperandAsJSON json_op = {nullptr, code};
  for (const PhiInstruction* phi : block->phis()) {
    if (needs_comma) os << ",";
    needs_comma = true;
    json_op.op_ = &phi->output();
    os << "{\"output\" : " << json_op << ",";
    os << "\"operands\": [";
    bool op_needs_comma = false;
    for (int input : phi->operands()) {
      if (op_needs_comma) os << ",";
      op_needs_comma = true;
      os << "\"v" << input << "\"";
    }
    os << "]}";
  }
  os << "],";

  os << "\"instructions\": [";
  InstructionAsJSON json_instr = {-1, nullptr, code};
  need_comma = false;
  for (int j = block->first_instruction_index();
       j <= block->last_instruction_index(); j++) {
    if (need_comma) os << ",";
    need_comma = true;
    json_instr.index_ = j;
    json_instr.instr_ = code->InstructionAt(j);
    os << json_instr;
  }
  os << "]";
  os << "}";

  return os;
}

std::ostream& operator<<(std::ostream& os, const InstructionSequenceAsJSON& s) {
  const InstructionSequence* code = s.sequence_;

  os << "[";

  bool need_comma = false;
  for (int i = 0; i < code->InstructionBlockCount(); i++) {
    if (need_comma) os << ",";
    need_comma = true;
    os << InstructionBlockAsJSON{
        code->InstructionBlockAt(RpoNumber::FromInt(i)), code};
  }
  os << "]";

  return os;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
