// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/wasm/gdb-server/wasm-module-debug.h"

#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/execution/frames-inl.h"
#include "src/execution/frames.h"
#include "src/objects/script.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace gdb_server {

WasmModuleDebug::WasmModuleDebug(v8::Isolate* isolate,
                                 Local<debug::WasmScript> wasm_script) {
  DCHECK_EQ(Script::Type::kWasm, Utils::OpenHandle(*wasm_script)->type());

  isolate_ = isolate;
  wasm_script_ = Global<debug::WasmScript>(isolate, wasm_script);
}

std::string WasmModuleDebug::GetModuleName() const {
  v8::Local<debug::WasmScript> wasm_script = wasm_script_.Get(isolate_);
  v8::Local<v8::String> name;
  std::string module_name;
  if (wasm_script->Name().ToLocal(&name)) {
    module_name = *(v8::String::Utf8Value(isolate_, name));
  }
  return module_name;
}

Handle<WasmInstanceObject> WasmModuleDebug::GetFirstWasmInstance() {
  v8::Local<debug::WasmScript> wasm_script = wasm_script_.Get(isolate_);
  Handle<Script> script = Utils::OpenHandle(*wasm_script);

  Handle<WeakArrayList> weak_instance_list(script->wasm_weak_instance_list(),
                                           GetIsolate());
  if (weak_instance_list->length() > 0) {
    Tagged<MaybeObject> maybe_instance = weak_instance_list->Get(0);
    if (maybe_instance.IsWeak()) {
      Handle<WasmInstanceObject> instance(
          WasmInstanceObject::cast(maybe_instance.GetHeapObjectAssumeWeak()),
          GetIsolate());
      return instance;
    }
  }
  return Handle<WasmInstanceObject>::null();
}

int GetLEB128Size(base::Vector<const uint8_t> module_bytes, int offset) {
  int index = offset;
  while (module_bytes[index] & 0x80) index++;
  return index + 1 - offset;
}

int ReturnPc(const NativeModule* native_module, int pc) {
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  uint8_t opcode = wire_bytes[pc];
  switch (opcode) {
    case kExprCallFunction: {
      // skip opcode
      pc++;
      // skip function index
      return pc + GetLEB128Size(wire_bytes, pc);
    }
    case kExprCallIndirect: {
      // skip opcode
      pc++;
      // skip signature index
      pc += GetLEB128Size(wire_bytes, pc);
      // skip table index
      return pc + GetLEB128Size(wire_bytes, pc);
    }
    default:
      UNREACHABLE();
  }
}

// static
std::vector<wasm_addr_t> WasmModuleDebug::GetCallStack(
    uint32_t debug_context_id, Isolate* isolate) {
  std::vector<wasm_addr_t> call_stack;
  for (StackFrameIterator frame_it(isolate); !frame_it.done();
       frame_it.Advance()) {
    StackFrame* const frame = frame_it.frame();
    switch (frame->type()) {
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION:
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH:
      case StackFrame::INTERPRETED:
      case StackFrame::BASELINE:
      case StackFrame::MAGLEV:
      case StackFrame::TURBOFAN:
      case StackFrame::BUILTIN:
      case StackFrame::WASM: {
        // A standard frame may include many summarized frames, due to inlining.
        std::vector<FrameSummary> frames;
        CommonFrame::cast(frame)->Summarize(&frames);
        for (size_t i = frames.size(); i-- != 0;) {
          int offset = 0;
          Handle<Script> script;

          auto& summary = frames[i];
          if (summary.IsJavaScript()) {
            FrameSummary::JavaScriptFrameSummary const& java_script =
                summary.AsJavaScript();
            offset = java_script.code_offset();
            script = Handle<Script>::cast(java_script.script());
          } else if (summary.IsWasm()) {
            FrameSummary::WasmFrameSummary const& wasm = summary.AsWasm();
            offset = GetWasmFunctionOffset(wasm.wasm_instance()->module(),
                                           wasm.function_index()) +
                     wasm.code_offset();
            script = wasm.script();

            bool zeroth_frame = call_stack.empty();
            if (!zeroth_frame) {
              const NativeModule* native_module =
                  wasm.wasm_instance()->module_object().native_module();
              offset = ReturnPc(native_module, offset);
            }
          }

          if (offset > 0) {
            call_stack.push_back(
                {debug_context_id << 16 | script->id(), uint32_t(offset)});
          }
        }
        break;
      }

      case StackFrame::BUILTIN_EXIT:
      default:
        // ignore the frame.
        break;
    }
  }
  if (call_stack.empty()) call_stack.push_back({1, 0});
  return call_stack;
}

// static
std::vector<FrameSummary> WasmModuleDebug::FindWasmFrame(
    DebuggableStackFrameIterator* frame_it, uint32_t* frame_index) {
  while (!frame_it->done()) {
    StackFrame* const frame = frame_it->frame();
    switch (frame->type()) {
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION:
      case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH:
      case StackFrame::INTERPRETED:
      case StackFrame::BASELINE:
      case StackFrame::MAGLEV:
      case StackFrame::TURBOFAN:
      case StackFrame::BUILTIN:
      case StackFrame::WASM: {
        // A standard frame may include many summarized frames, due to inlining.
        std::vector<FrameSummary> frames;
        CommonFrame::cast(frame)->Summarize(&frames);
        const size_t frame_count = frames.size();
        DCHECK_GT(frame_count, 0);

        if (frame_count > *frame_index) {
          if (frame_it->is_wasm())
            return frames;
          else
            return {};
        } else {
          *frame_index -= frame_count;
          frame_it->Advance();
        }
        break;
      }

      case StackFrame::BUILTIN_EXIT:
      default:
        // ignore the frame.
        break;
    }
  }
  return {};
}

// static
Handle<WasmInstanceObject> WasmModuleDebug::GetWasmInstance(
    Isolate* isolate, uint32_t frame_index) {
  DebuggableStackFrameIterator frame_it(isolate);
  std::vector<FrameSummary> frames = FindWasmFrame(&frame_it, &frame_index);
  if (frames.empty()) {
    return Handle<WasmInstanceObject>::null();
  }

  int reversed_index = static_cast<int>(frames.size() - 1 - frame_index);
  const FrameSummary::WasmFrameSummary& summary =
      frames[reversed_index].AsWasm();
  return summary.wasm_instance();
}

// static
bool WasmModuleDebug::GetWasmGlobal(Isolate* isolate, uint32_t frame_index,
                                    uint32_t index, uint8_t* buffer,
                                    uint32_t buffer_size, uint32_t* size) {
  HandleScope handles(isolate);

  Handle<WasmInstanceObject> instance = GetWasmInstance(isolate, frame_index);
  if (!instance.is_null()) {
    Handle<WasmModuleObject> module_object(instance->module_object(), isolate);
    const wasm::WasmModule* module = module_object->module();
    if (index < module->globals.size()) {
      wasm::WasmValue wasm_value =
          WasmInstanceObject::GetGlobalValue(instance, module->globals[index]);
      return GetWasmValue(wasm_value, buffer, buffer_size, size);
    }
  }
  return false;
}

// static
bool WasmModuleDebug::GetWasmLocal(Isolate* isolate, uint32_t frame_index,
                                   uint32_t index, uint8_t* buffer,
                                   uint32_t buffer_size, uint32_t* size) {
  HandleScope handles(isolate);

  DebuggableStackFrameIterator frame_it(isolate);
  std::vector<FrameSummary> frames = FindWasmFrame(&frame_it, &frame_index);
  if (frames.empty()) {
    return false;
  }

  int reversed_index = static_cast<int>(frames.size() - 1 - frame_index);
  const FrameSummary& summary = frames[reversed_index];
  if (summary.IsWasm()) {
    Handle<WasmInstanceObject> instance = summary.AsWasm().wasm_instance();
    if (!instance.is_null()) {
      Handle<WasmModuleObject> module_object(instance->module_object(),
                                             isolate);
      wasm::NativeModule* native_module = module_object->native_module();
      DebugInfo* debug_info = native_module->GetDebugInfo();
      if (static_cast<uint32_t>(
              debug_info->GetNumLocals(frame_it.frame()->pc())) > index) {
        wasm::WasmValue wasm_value = debug_info->GetLocalValue(
            index, frame_it.frame()->pc(), frame_it.frame()->fp(),
            frame_it.frame()->callee_fp());
        return GetWasmValue(wasm_value, buffer, buffer_size, size);
      }
    }
  }
  return false;
}

// static
bool WasmModuleDebug::GetWasmStackValue(Isolate* isolate, uint32_t frame_index,
                                        uint32_t index, uint8_t* buffer,
                                        uint32_t buffer_size, uint32_t* size) {
  HandleScope handles(isolate);

  DebuggableStackFrameIterator frame_it(isolate);
  std::vector<FrameSummary> frames = FindWasmFrame(&frame_it, &frame_index);
  if (frames.empty()) {
    return false;
  }

  int reversed_index = static_cast<int>(frames.size() - 1 - frame_index);
  const FrameSummary& summary = frames[reversed_index];
  if (summary.IsWasm()) {
    Handle<WasmInstanceObject> instance = summary.AsWasm().wasm_instance();
    if (!instance.is_null()) {
      Handle<WasmModuleObject> module_object(instance->module_object(),
                                             isolate);
      wasm::NativeModule* native_module = module_object->native_module();
      DebugInfo* debug_info = native_module->GetDebugInfo();
      if (static_cast<uint32_t>(
              debug_info->GetStackDepth(frame_it.frame()->pc())) > index) {
        WasmValue wasm_value = debug_info->GetStackValue(
            index, frame_it.frame()->pc(), frame_it.frame()->fp(),
            frame_it.frame()->callee_fp());
        return GetWasmValue(wasm_value, buffer, buffer_size, size);
      }
    }
  }
  return false;
}

uint32_t WasmModuleDebug::GetWasmMemory(Isolate* isolate, uint32_t offset,
                                        uint8_t* buffer, uint32_t size) {
  HandleScope handles(isolate);

  uint32_t bytes_read = 0;
  Handle<WasmInstanceObject> instance = GetFirstWasmInstance();
  if (!instance.is_null()) {
    uint8_t* mem_start = instance->memory_start();
    size_t mem_size = instance->memory_size();
    if (static_cast<uint64_t>(offset) + size <= mem_size) {
      memcpy(buffer, mem_start + offset, size);
      bytes_read = size;
    } else if (offset < mem_size) {
      bytes_read = static_cast<uint32_t>(mem_size) - offset;
      memcpy(buffer, mem_start + offset, bytes_read);
    }
  }
  return bytes_read;
}

uint32_t WasmModuleDebug::GetWasmData(Isolate* isolate, uint32_t offset,
                                      uint8_t* buffer, uint32_t size) {
  HandleScope handles(isolate);

  uint32_t bytes_read = 0;
  Handle<WasmInstanceObject> instance = GetFirstWasmInstance();
  if (!instance.is_null()) {
    Handle<WasmModuleObject> module_object(instance->module_object(), isolate);
    const wasm::WasmModule* module = module_object->module();
    if (!module->data_segments.empty()) {
      const WasmDataSegment& segment = module->data_segments[0];
      uint32_t data_offset = EvalUint32InitExpr(instance, segment.dest_addr);
      offset += data_offset;

      uint8_t* mem_start = instance->memory_start();
      size_t mem_size = instance->memory_size();
      if (static_cast<uint64_t>(offset) + size <= mem_size) {
        memcpy(buffer, mem_start + offset, size);
        bytes_read = size;
      } else if (offset < mem_size) {
        bytes_read = static_cast<uint32_t>(mem_size) - offset;
        memcpy(buffer, mem_start + offset, bytes_read);
      }
    }
  }
  return bytes_read;
}

uint32_t WasmModuleDebug::GetWasmModuleBytes(wasm_addr_t wasm_addr,
                                             uint8_t* buffer, uint32_t size) {
  uint32_t bytes_read = 0;
  // Any instance will work.
  Handle<WasmInstanceObject> instance = GetFirstWasmInstance();
  if (!instance.is_null()) {
    Handle<WasmModuleObject> module_object(instance->module_object(),
                                           GetIsolate());
    wasm::NativeModule* native_module = module_object->native_module();
    const wasm::ModuleWireBytes wire_bytes(native_module->wire_bytes());
    uint32_t offset = wasm_addr.Offset();
    if (offset < wire_bytes.length()) {
      uint32_t module_size = static_cast<uint32_t>(wire_bytes.length());
      bytes_read = module_size - offset >= size ? size : module_size - offset;
      memcpy(buffer, wire_bytes.start() + offset, bytes_read);
    }
  }
  return bytes_read;
}

bool WasmModuleDebug::AddBreakpoint(uint32_t offset, int* breakpoint_id) {
  v8::Local<debug::WasmScript> wasm_script = wasm_script_.Get(isolate_);
  Handle<Script> script = Utils::OpenHandle(*wasm_script);
  Handle<String> condition = GetIsolate()->factory()->empty_string();
  int breakpoint_address = static_cast<int>(offset);
  return GetIsolate()->debug()->SetBreakPointForScript(
      script, condition, &breakpoint_address, breakpoint_id);
}

void WasmModuleDebug::RemoveBreakpoint(uint32_t offset, int breakpoint_id) {
  v8::Local<debug::WasmScript> wasm_script = wasm_script_.Get(isolate_);
  Handle<Script> script = Utils::OpenHandle(*wasm_script);
  GetIsolate()->debug()->RemoveBreakpointForWasmScript(script, breakpoint_id);
}

void WasmModuleDebug::PrepareStep() {
  i::Isolate* isolate = GetIsolate();
  DebugScope debug_scope(isolate->debug());
  debug::PrepareStep(reinterpret_cast<v8::Isolate*>(isolate),
                     debug::StepAction::StepInto);
}

template <typename T>
bool StoreValue(const T& value, uint8_t* buffer, uint32_t buffer_size,
                uint32_t* size) {
  *size = sizeof(value);
  if (*size > buffer_size) return false;
  memcpy(buffer, &value, *size);
  return true;
}

// static
bool WasmModuleDebug::GetWasmValue(const wasm::WasmValue& wasm_value,
                                   uint8_t* buffer, uint32_t buffer_size,
                                   uint32_t* size) {
  switch (wasm_value.type().kind()) {
    case wasm::kI32:
      return StoreValue(wasm_value.to_i32(), buffer, buffer_size, size);
    case wasm::kI64:
      return StoreValue(wasm_value.to_i64(), buffer, buffer_size, size);
    case wasm::kF32:
      return StoreValue(wasm_value.to_f32(), buffer, buffer_size, size);
    case wasm::kF64:
      return StoreValue(wasm_value.to_f64(), buffer, buffer_size, size);
    case wasm::kS128:
      return StoreValue(wasm_value.to_s128(), buffer, buffer_size, size);
    case wasm::kRef:
    case wasm::kRefNull:
    case wasm::kRtt:
    case wasm::kVoid:
    case wasm::kBottom:
      // Not supported
      return false;
  }
}

}  // namespace gdb_server
}  // namespace wasm
}  // namespace internal
}  // namespace v8
