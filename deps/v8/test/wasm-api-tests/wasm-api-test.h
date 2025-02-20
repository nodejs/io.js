// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_WASM_API_TESTS_WASM_API_TEST_H_
#define TEST_WASM_API_TESTS_WASM_API_TEST_H_

#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "testing/gtest/include/gtest/gtest.h"

#define LIBWASM_STATIC 1
#include "third_party/wasm-api/wasm.hh"

namespace v8 {
namespace internal {
namespace wasm {

using ::wasm::Engine;
using ::wasm::Extern;
using ::wasm::Foreign;
using ::wasm::Func;
using ::wasm::FuncType;
using ::wasm::Global;
using ::wasm::Instance;
using ::wasm::Memory;
using ::wasm::Module;
using ::wasm::own;
using ::wasm::ownvec;
using ::wasm::Ref;
using ::wasm::Store;
using ::wasm::Table;
using ::wasm::Trap;
using ::wasm::Val;
using ::wasm::ValType;
using ::wasm::vec;

class WasmCapiTest : public ::testing::Test {
 public:
  WasmCapiTest()
      : Test(),
        engine_(Engine::make()),
        allocator_(std::make_unique<AccountingAllocator>()),
        zone_(std::make_unique<Zone>(allocator_.get(), ZONE_NAME)),
        wire_bytes_(zone_.get()),
        builder_(zone_->New<WasmModuleBuilder>(zone_.get())),
        exports_(ownvec<Extern>::make()),
        binary_(vec<byte_t>::make()),
        wasm_i_i_sig_(1, 1, wasm_i_i_sig_types_) {
    store_ = Store::make(engine_.get());
    cpp_i_i_sig_ = FuncType::make(
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)),
        ownvec<ValType>::make(ValType::make(::wasm::ValKind::I32)));
  }

  bool Validate() {
    if (binary_.size() == 0) {
      builder_->WriteTo(&wire_bytes_);
      size_t size = wire_bytes_.end() - wire_bytes_.begin();
      binary_ = vec<byte_t>::make(
          size,
          reinterpret_cast<byte_t*>(const_cast<uint8_t*>(wire_bytes_.begin())));
    }

    return Module::validate(store_.get(), binary_);
  }

  void Compile() {
    if (binary_.size() == 0) {
      builder_->WriteTo(&wire_bytes_);
      size_t size = wire_bytes_.end() - wire_bytes_.begin();
      binary_ = vec<byte_t>::make(
          size,
          reinterpret_cast<byte_t*>(const_cast<uint8_t*>(wire_bytes_.begin())));
    }

    module_ = Module::make(store_.get(), binary_);
    DCHECK_NE(module_.get(), nullptr);
  }

  void Instantiate(vec<Extern*>& imports) {
    Compile();
    instance_ = Instance::make(store_.get(), module_.get(), imports);
    DCHECK_NE(instance_.get(), nullptr);
    exports_ = instance_->exports();
  }

  void AddExportedFunction(base::Vector<const char> name, uint8_t code[],
                           size_t code_size, FunctionSig* sig) {
    WasmFunctionBuilder* fun = builder()->AddFunction(sig);
    fun->EmitCode(code, static_cast<uint32_t>(code_size));
    fun->Emit(kExprEnd);
    builder()->AddExport(name, fun);
  }

  void AddFunction(uint8_t code[], size_t code_size, FunctionSig* sig) {
    WasmFunctionBuilder* fun = builder()->AddFunction(sig);
    fun->EmitCode(code, static_cast<uint32_t>(code_size));
    fun->Emit(kExprEnd);
  }

  Func* GetExportedFunction(size_t index) {
    DCHECK_GT(exports_.size(), index);
    Extern* exported = exports_[index].get();
    DCHECK_EQ(exported->kind(), ::wasm::ExternKind::FUNC);
    Func* func = exported->func();
    DCHECK_NE(func, nullptr);
    return func;
  }

  Global* GetExportedGlobal(size_t index) {
    DCHECK_GT(exports_.size(), index);
    Extern* exported = exports_[index].get();
    DCHECK_EQ(exported->kind(), ::wasm::ExternKind::GLOBAL);
    Global* global = exported->global();
    DCHECK_NE(global, nullptr);
    return global;
  }

  Memory* GetExportedMemory(size_t index) {
    DCHECK_GT(exports_.size(), index);
    Extern* exported = exports_[index].get();
    DCHECK_EQ(exported->kind(), ::wasm::ExternKind::MEMORY);
    Memory* memory = exported->memory();
    DCHECK_NE(memory, nullptr);
    return memory;
  }

  Table* GetExportedTable(size_t index) {
    DCHECK_GT(exports_.size(), index);
    Extern* exported = exports_[index].get();
    DCHECK_EQ(exported->kind(), ::wasm::ExternKind::TABLE);
    Table* table = exported->table();
    DCHECK_NE(table, nullptr);
    return table;
  }

  void ResetModule() { module_.reset(); }

  void Shutdown() {
    exports_.reset();
    instance_.reset();
    module_.reset();
    store_.reset();
    builder_ = nullptr;
    zone_.reset();
    allocator_.reset();
    engine_.reset();
  }

  WasmModuleBuilder* builder() { return builder_; }
  Engine* engine() { return engine_.get(); }
  Store* store() { return store_.get(); }
  Module* module() { return module_.get(); }
  Instance* instance() { return instance_.get(); }
  const ownvec<Extern>& exports() { return exports_; }
  base::Vector<const uint8_t> wire_bytes() {
    return base::VectorOf(wire_bytes_);
  }

  FunctionSig* wasm_i_i_sig() { return &wasm_i_i_sig_; }
  FuncType* cpp_i_i_sig() { return cpp_i_i_sig_.get(); }

 private:
  own<Engine> engine_;
  std::unique_ptr<AccountingAllocator> allocator_;
  std::unique_ptr<Zone> zone_;
  ZoneBuffer wire_bytes_;
  WasmModuleBuilder* builder_;
  own<Store> store_;
  own<Module> module_;
  own<Instance> instance_;
  ownvec<Extern> exports_;
  vec<byte_t> binary_;
  own<FuncType> cpp_i_i_sig_;
  ValueType wasm_i_i_sig_types_[2] = {kWasmI32, kWasmI32};
  FunctionSig wasm_i_i_sig_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // TEST_WASM_API_TESTS_WASM_API_TEST_H_
