#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


auto get_export_memory(wasm::ownvec<wasm::Extern>& exports, size_t i) -> wasm::Memory* {
  if (exports.size() <= i || !exports[i]->memory()) {
    std::cout << "> Error accessing memory export " << i << "!" << std::endl;
    exit(1);
  }
  return exports[i]->memory();
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

template<class... Args>
void check_ok(const wasm::Func* func, Args... xs) {
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(xs)...);
  auto results = wasm::vec<wasm::Val>::make();
  if (func->call(args, results)) {
    std::cout << "> Error on result, expected return" << std::endl;
    exit(1);
  }
}

template<class... Args>
void check_trap(const wasm::Func* func, Args... xs) {
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(xs)...);
  auto results = wasm::vec<wasm::Val>::make();
  if (!func->call(args, results)) {
    std::cout << "> Error on result, expected trap" << std::endl;
    exit(1);
  }
}

template<class... Args>
auto call(const wasm::Func* func, Args... xs) -> int32_t {
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(xs)...);
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (func->call(args, results)) {
    std::cout << "> Error on result, expected return" << std::endl;
    exit(1);
  }
  return results[0].i32();
}


void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("memory.wasm");
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
  auto memory = get_export_memory(exports, i++);
  auto size_func = get_export_func(exports, i++);
  auto load_func = get_export_func(exports, i++);
  auto store_func = get_export_func(exports, i++);

  // Try cloning.
  assert(memory->copy()->same(memory));

  // Check initial memory.
  std::cout << "Checking memory..." << std::endl;
  check(memory->size(), 2u);
  check(memory->data_size(), 0x20000u);
  check(memory->data()[0], 0);
  check(memory->data()[0x1000], 1);
  check(memory->data()[0x1003], 4);

  check(call(size_func), 2);
  check(call(load_func, 0), 0);
  check(call(load_func, 0x1000), 1);
  check(call(load_func, 0x1003), 4);
  check(call(load_func, 0x1ffff), 0);
  check_trap(load_func, 0x20000);

  // Mutate memory.
  std::cout << "Mutating memory..." << std::endl;
  memory->data()[0x1003] = 5;
  check_ok(store_func, 0x1002, 6);
  check_trap(store_func, 0x20000, 0);

  check(memory->data()[0x1002], 6);
  check(memory->data()[0x1003], 5);
  check(call(load_func, 0x1002), 6);
  check(call(load_func, 0x1003), 5);

  // Grow memory.
  std::cout << "Growing memory..." << std::endl;
  check(memory->grow(1), true);
  check(memory->size(), 3u);
  check(memory->data_size(), 0x30000u);

  check(call(load_func, 0x20000), 0);
  check_ok(store_func, 0x20000, 0);
  check_trap(load_func, 0x30000);
  check_trap(store_func, 0x30000, 0);

  check(memory->grow(1), false);
  check(memory->grow(0), true);

  // Create stand-alone memory.
  // TODO(wasm+): Once Wasm allows multiple memories, turn this into import.
  std::cout << "Creating stand-alone memory..." << std::endl;
  auto memorytype = wasm::MemoryType::make(wasm::Limits(5, 5));
  auto memory2 = wasm::Memory::make(store, memorytype.get());
  check(memory2->size(), 5u);
  check(memory2->grow(1), false);
  check(memory2->grow(0), true);

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

