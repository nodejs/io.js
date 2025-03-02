#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


// A function to be called from Wasm code.
auto hello_callback(const wasm::vec<wasm::Val>& args,
                    wasm::vec<wasm::Val>& results) -> wasm::own<wasm::Trap> {
  std::cout << "Calling back..." << std::endl;
  std::cout << "> Hello world!" << std::endl;
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
  std::ifstream file("serialize.wasm");
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

  // Serialize module.
  std::cout << "Serializing module..." << std::endl;
  auto serialized = module->serialize();

  // Deserialize module.
  std::cout << "Deserializing module..." << std::endl;
  auto deserialized = wasm::Module::deserialize(store, serialized);
  if (!deserialized) {
    std::cout << "> Error deserializing module!" << std::endl;
    exit(1);
  }

  // Create external print functions.
  std::cout << "Creating callback..." << std::endl;
  auto hello_type = wasm::FuncType::make(
    wasm::ownvec<wasm::ValType>::make(), wasm::ownvec<wasm::ValType>::make()
  );
  auto hello_func = wasm::Func::make(store, hello_type.get(), hello_callback);

  // Instantiate.
  std::cout << "Instantiating deserialized module..." << std::endl;
  auto imports = wasm::vec<wasm::Extern*>::make(hello_func.get());
  auto instance = wasm::Instance::make(store, deserialized.get(), imports);
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
  auto args = wasm::vec<wasm::Val>::make();
  auto results = wasm::vec<wasm::Val>::make();
  if (run_func->call(args, results)) {
    std::cout << "> Error calling function!" << std::endl;
    exit(1);
  }

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

