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
  std::cout << "> " << args[0].i32();
  std::cout << " " << args[1].i64();
  std::cout << " " << args[2].i64();
  std::cout << " " << args[3].i32() << std::endl;
  results[0] = args[3].copy();
  results[1] = args[1].copy();
  results[2] = args[2].copy();
  results[3] = args[0].copy();
  return nullptr;
}

void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("multi.wasm");
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

  // Create external print functions.
  std::cout << "Creating callback..." << std::endl;
  auto tuple = wasm::ownvec<wasm::ValType>::make(
      wasm::ValType::make(wasm::ValKind::I32),
      wasm::ValType::make(wasm::ValKind::I64),
      wasm::ValType::make(wasm::ValKind::I64),
      wasm::ValType::make(wasm::ValKind::I32));
  auto callback_type =
    wasm::FuncType::make(tuple.deep_copy(), tuple.deep_copy());
  auto callback_func = wasm::Func::make(store, callback_type.get(), callback);

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make(callback_func.get());
  auto instance = wasm::Instance::make(store, module.get(), imports);
  if (!instance) {
    std::cout << "> Error instantiating module!" << std::endl;
    exit(1);
  }

  // Extract export.
  std::cout << "Extracting export..." << std::endl;
  auto exports = instance->exports();
  if (exports.size() == 0 || exports[0]->kind() != wasm::ExternKind::FUNC ||
      !exports[0]->func()) {
    std::cout << "> Error accessing export!" << std::endl;
    exit(1);
  }
  auto run_func = exports[0]->func();

  // Call.
  std::cout << "Calling export..." << std::endl;
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(1), wasm::Val::i64(2),
                                         wasm::Val::i64(3), wasm::Val::i32(4));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(4);
  if (wasm::own<wasm::Trap> trap = run_func->call(args, results)) {
    std::cout << "> Error calling function! " << trap->message().get() << std::endl;
    exit(1);
  }

  // Print result.
  std::cout << "Printing result..." << std::endl;
  std::cout << "> " << results[0].i32();
  std::cout << " " << results[1].i64();
  std::cout << " " << results[2].i64();
  std::cout << " " << results[3].i32() << std::endl;

  assert(results[0].i32() == 4);
  assert(results[1].i64() == 3);
  assert(results[2].i64() == 2);
  assert(results[3].i32() == 1);

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

