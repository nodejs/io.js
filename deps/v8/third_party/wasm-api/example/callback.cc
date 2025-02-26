#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"

// Print a Wasm value
auto operator<<(std::ostream& out, const wasm::Val& val) -> std::ostream& {
  switch (val.kind()) {
    case wasm::ValKind::I32: {
      out << val.i32();
    } break;
    case wasm::ValKind::I64: {
      out << val.i64();
    } break;
    case wasm::ValKind::F32: {
      out << val.f32();
    } break;
    case wasm::ValKind::F64: {
      out << val.f64();
    } break;
    case wasm::ValKind::EXTERNREF:
    case wasm::ValKind::FUNCREF: {
      if (val.ref() == nullptr) {
        out << "null";
      } else {
        out << "ref(" << val.ref() << ")";
      }
    } break;
  }
  return out;
}

// A function to be called from Wasm code.
auto print_callback(const wasm::vec<wasm::Val>& args,
                    wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  std::cout << "Calling back..." << std::endl << "> " << args[0] << std::endl;
  results[0] = args[0].copy();
  return nullptr;
}

// A function closure.
auto closure_callback(void* env, const wasm::vec<wasm::Val>& args,
                      wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  auto i = *reinterpret_cast<int*>(env);
  std::cout << "Calling back closure..." << std::endl;
  std::cout << "> " << i << std::endl;
  results[0] = wasm::Val::i32(static_cast<int32_t>(i));
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
  std::ifstream file("callback.wasm");
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
  auto print_type =
      wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)),
                           wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)));
  auto print_func = wasm::Func::make(store, print_type.get(), print_callback);

  // Creating closure.
  std::cout << "Creating closure..." << std::endl;
  int i = 42;
  auto closure_type =
      wasm::FuncType::make(wasm::ownvec<wasm::ValType>::make(),
                           wasm::ownvec<wasm::ValType>::make(
                               wasm::ValType::make(wasm::ValKind::I32)));
  auto closure_func = wasm::Func::make(store, closure_type.get(), closure_callback, &i);

  // Instantiate.
  std::cout << "Instantiating module..." << std::endl;
  auto imports =
      wasm::vec<wasm::Extern*>::make(print_func.get(), closure_func.get());
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
  auto args = wasm::vec<wasm::Val>::make(wasm::Val::i32(3), wasm::Val::i32(4));
  auto results = wasm::vec<wasm::Val>::make_uninitialized(1);
  if (run_func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }

  // Print result.
  std::cout << "Printing result..." << std::endl;
  std::cout << "> " << results[0].i32() << std::endl;

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

