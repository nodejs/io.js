#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


// A function to be called from Wasm code.
auto callback(const wasm::vec<wasm::Val>& args, wasm::vec<wasm::Val>& results)
    -> wasm::own<wasm::Trap> {
  std::cout << "Calling back..." << std::endl;
  std::cout << "> " << (args[0].ref() ? args[0].ref()->get_host_info() : nullptr) << std::endl;
  results[0] = args[0].copy();
  return nullptr;
}

auto get_export_func(const wasm::ownvec<wasm::Extern>& exports, size_t i) -> const wasm::Func* {
  if (exports.size() <= i || !exports[i]->func()) {
    std::cout << "> Error accessing function export " << i << "/" << exports.size() << "!" << std::endl;
    exit(1);
  }
  return exports[i]->func();
}

auto get_export_global(wasm::ownvec<wasm::Extern>& exports, size_t i) -> wasm::Global* {
  if (exports.size() <= i || !exports[i]->global()) {
    std::cout << "> Error accessing global export " << i << "!" << std::endl;
    exit(1);
  }
  return exports[i]->global();
}

auto get_export_table(wasm::ownvec<wasm::Extern>& exports, size_t i) -> wasm::Table* {
  if (exports.size() <= i || !exports[i]->table()) {
    std::cout << "> Error accessing table export " << i << "!" << std::endl;
    exit(1);
  }
  return exports[i]->table();
}


void call_r_v(const wasm::Func* func, const wasm::Ref* ref) {
  std::cout << "call_r_v... " << std::flush;
  auto args = wasm::vec<wasm::Val>::make(
      wasm::Val::ref(ref ? ref->copy() : wasm::own<wasm::Ref>()));
  auto results = wasm::vec<wasm::Val>::make();
  if (func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }
  std::cout << "okay" << std::endl;
}

auto call_v_r(const wasm::Func* func) -> wasm::own<wasm::Ref> {
  std::cout << "call_v_r... " << std::flush;
  auto args = wasm::vec<wasm::Val>::make();
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }
  std::cout << "okay" << std::endl;
  return results[0].release_ref();
}

auto call_r_r(const wasm::Func* func, const wasm::Ref* ref) -> wasm::own<wasm::Ref> {
  std::cout << "call_r_r... " << std::flush;
  auto args = wasm::vec<wasm::Val>::make(
      wasm::Val::ref(ref ? ref->copy() : wasm::own<wasm::Ref>()));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }
  std::cout << "okay" << std::endl;
  return results[0].release_ref();
}

void call_ir_v(const wasm::Func* func, int32_t i, const wasm::Ref* ref) {
  std::cout << "call_ir_v... " << std::flush;
  auto args = wasm::vec<wasm::Val>::make(
      wasm::Val::i32(i),
      wasm::Val::ref(ref ? ref->copy() : wasm::own<wasm::Ref>()));
  auto results = wasm::vec<wasm::Val>::make();
  if (func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }
  std::cout << "okay" << std::endl;
}

auto call_i_r(const wasm::Func* func, int32_t i) -> wasm::own<wasm::Ref> {
  std::cout << "call_i_r... " << std::flush;
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(i));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }
  std::cout << "okay" << std::endl;
  return results[0].release_ref();
}

void check(wasm::own<wasm::Ref> actual, const wasm::Ref* expected) {
  if (actual.get() != expected &&
      !(actual && expected && actual->same(expected))) {
    std::cout << "> Error reading reference, expected "
      << (expected ? expected->get_host_info() : nullptr) << ", got "
      << (actual ? actual->get_host_info() : nullptr) << std::endl;
    exit(1);
  }
}

void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("hostref.wasm");
  file.seekg(0, std::ios_base::end);
  auto file_size = file.tellg();
  file.seekg(0);
  auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
  file.read(binary.get(), file_size);
  file.close();
  if (file.fail()) {
    std::cout << "> Error loading module!" << std::endl;
    return;
  }

  // Compile.
  std::cout << "Compiling module..." << std::endl;
  auto module = wasm::Module::make(store, binary);
  if (!module) {
    std::cout << "> Error compiling module!" << std::endl;
    return;
  }

  // Create external callback function.
  std::cout << "Creating callback..." << std::endl;
  auto callback_type =
      wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::EXTERNREF)),
                           wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::EXTERNREF)));
  auto callback_func = wasm::Func::make(store, callback_type.get(), callback);

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make(callback_func.get());
  auto instance = wasm::Instance::make(store, module.get(), imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    return;
  }

  // Extract export.
  std::cout << "Extracting exports..." << std::endl;
  auto exports = instance->exports();
  size_t i = 0;
  auto global = get_export_global(exports, i++);
  auto table = get_export_table(exports, i++);
  auto global_set = get_export_func(exports, i++);
  auto global_get = get_export_func(exports, i++);
  auto table_set = get_export_func(exports, i++);
  auto table_get = get_export_func(exports, i++);
  auto func_call = get_export_func(exports, i++);

  // Create host references.
  std::cout << "Creating host references..." << std::endl;
  auto host1 = wasm::Foreign::make(store);
  auto host2 = wasm::Foreign::make(store);
  host1->set_host_info(reinterpret_cast<void*>(1));
  host2->set_host_info(reinterpret_cast<void*>(2));

  // Some sanity checks.
  check(nullptr, nullptr);
  check(host1->copy(), host1.get());
  check(host2->copy(), host2.get());

  wasm::Val val = wasm::Val::ref(host1->copy());
  check(val.ref()->copy(), host1.get());
  auto ref = val.release_ref();
  assert(val.ref() == nullptr);
  check(ref->copy(), host1.get());

  // Interact.
  std::cout << "Accessing global..." << std::endl;
  check(call_v_r(global_get), nullptr);
  call_r_v(global_set, host1.get());
  check(call_v_r(global_get), host1.get());
  call_r_v(global_set, host2.get());
  check(call_v_r(global_get), host2.get());
  call_r_v(global_set, nullptr);
  check(call_v_r(global_get), nullptr);

  check(global->get().release_ref(), nullptr);
  global->set(wasm::Val(host2->copy()));
  check(call_v_r(global_get), host2.get());
  check(global->get().release_ref(), host2.get());

  std::cout << "Accessing table..." << std::endl;
  check(call_i_r(table_get, 0), nullptr);
  check(call_i_r(table_get, 1), nullptr);
  call_ir_v(table_set, 0, host1.get());
  call_ir_v(table_set, 1, host2.get());
  check(call_i_r(table_get, 0), host1.get());
  check(call_i_r(table_get, 1), host2.get());
  call_ir_v(table_set, 0, nullptr);
  check(call_i_r(table_get, 0), nullptr);

  check(table->get(2), nullptr);
  table->set(2, host1.get());
  check(call_i_r(table_get, 2), host1.get());
  check(table->get(2), host1.get());

  std::cout << "Accessing function..." << std::endl;
  check(call_r_r(func_call, nullptr), nullptr);
  check(call_r_r(func_call, host1.get()), host1.get());
  check(call_r_r(func_call, host2.get()), host2.get());

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

