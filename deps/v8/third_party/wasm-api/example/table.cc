#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


// A function to be called from Wasm code.
auto neg_callback(const wasm::vec<wasm::Val>& args,
                  wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  std::cout << "Calling back..." << std::endl;
  results[0] = wasm::Val(-args[0].i32());
  return nullptr;
}

auto get_export_table(wasm::ownvec<wasm::Extern>& exports, size_t i) -> wasm::Table* {
  if (exports.size() <= i || !exports[i]->table()) {
    std::cout << "> Error accessing table export " << i << "!" << std::endl;
    exit(1);
  }
  return exports[i]->table();
}

auto get_export_func(const wasm::ownvec<wasm::Extern>& exports, size_t i) -> const wasm::Func* {
  if (exports.size() <= i || !exports[i]->func()) {
    std::cout << "> Error accessing function export " << i << "!" << std::endl;
    exit(1);
  }
  return exports[i]->func();
}

template<class T, class U>
void check(T actual, U expected) {
  if (actual != expected) {
    std::cout << "> Error on result, expected " << expected << ", got " << actual << std::endl;
    exit(1);
  }
}

void check(bool success) {
  if (! success) {
    std::cout << "> Error, expected success" << std::endl;
    exit(1);
  }
}

auto call(
  const wasm::Func* func, wasm::Val&& arg1, wasm::Val&& arg2
) -> wasm::Val {
  auto args = wasm::vec<wasm::Val>::make(std::move(arg1), std::move(arg2));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (func->call(args, results)) {
    std::cout << "> Error on result, expected return" << std::endl;
    exit(1);
  }
  return results[0].copy();
}

void check_trap(const wasm::Func* func, wasm::Val&& arg1, wasm::Val&& arg2) {
  auto args = wasm::vec<wasm::Val>::make(std::move(arg1), std::move(arg2));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (! func->call(args, results)) {
    std::cout << "> Error on result, expected trap" << std::endl;
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
  std::ifstream file("table.wasm");
  file.seekg(0, std::ios_base::end);
  auto file_size = file.tellg();
  file.seekg(0);
  auto binary = wasm::vec<byte_t>::make_uninitialized(file_size);
  file.read(binary.get(), file_size);
  file.close();
  if (file.fail()) {
    std::cout << "> Error loading module!" << std::endl;
    exit(1);
  }

  // Compile.
  std::cout << "Compiling module..." << std::endl;
  auto module = wasm::Module::make(store, binary);
  if (!module) {
    std::cout << "> Error compiling module!" << std::endl;
    exit(1);
  }

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make();
  auto instance = wasm::Instance::make(store, module.get(), imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    exit(1);
  }

  // Extract export.
  std::cout << "Extracting exports..." << std::endl;
  auto exports = instance->exports();
  size_t i = 0;
  auto table = get_export_table(exports, i++);
  auto call_indirect = get_export_func(exports, i++);
  auto f = get_export_func(exports, i++);
  auto g = get_export_func(exports, i++);

  // Create external function.
  std::cout << "Creating callback..." << std::endl;
  auto neg_type =
      wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)),
                           wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)));
  auto h = wasm::Func::make(store, neg_type.get(), neg_callback);

  // Try cloning.
  assert(table->copy()->same(table));

  // Check initial table.
  std::cout << "Checking table..." << std::endl;
  check(table->size(), 2u);
  check(table->get(0) == nullptr);
  check(table->get(1) != nullptr);
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(0));
  check(call(call_indirect, wasm::Val::i32(7), wasm::Val::i32(1)).i32(), 7);
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(2));

  // Mutate table.
  std::cout << "Mutating table..." << std::endl;
  check(table->set(0, g));
  check(table->set(1, nullptr));
  check(! table->set(2, f));
  check(table->get(0) != nullptr);
  check(table->get(1) == nullptr);
  check(call(call_indirect, wasm::Val::i32(7), wasm::Val::i32(0)).i32(), 666);
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(1));
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(2));

  // Grow table.
  std::cout << "Growing table..." << std::endl;
  check(table->grow(3));
  check(table->size(), 5u);
  check(table->set(2, f));
  check(table->set(3, h.get()));
  check(! table->set(5, nullptr));
  check(table->get(2) != nullptr);
  check(table->get(3) != nullptr);
  check(table->get(4) == nullptr);
  check(call(call_indirect, wasm::Val::i32(5), wasm::Val::i32(2)).i32(), 5);
  check(call(call_indirect, wasm::Val::i32(6), wasm::Val::i32(3)).i32(), -6);
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(4));
  check_trap(call_indirect, wasm::Val::i32(0), wasm::Val::i32(5));

  check(table->grow(2, f));
  check(table->size(), 7u);
  check(table->get(5) != nullptr);
  check(table->get(6) != nullptr);

  check(! table->grow(5));
  check(table->grow(3));
  check(table->grow(0));

  // Create stand-alone table.
  // TODO(wasm+): Once Wasm allows multiple tables, turn this into import.
  std::cout << "Creating stand-alone table..." << std::endl;
  auto tabletype = wasm::TableType::make(
      wasm::ValType::make(wasm::ValKind::FUNCREF), wasm::Limits(5, 5));
  auto table2 = wasm::Table::make(store, tabletype.get());
  check(table2->size() == 5);
  check(! table2->grow(1));
  check(table2->grow(0));

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}
