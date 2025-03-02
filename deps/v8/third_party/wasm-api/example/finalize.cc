#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


const int iterations = 100000;

int live_count = 0;

void finalize(void* data) {
  intptr_t i = reinterpret_cast<intptr_t>(data);
  if (i % (iterations / 10) == 0) {
    std::cout << "Finalizing #" << i << "..." << std::endl;
  }
  --live_count;
}

void run_in_store(wasm::Store* store) {
  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("finalize.wasm");
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
  std::cout << "Instantiating modules..." << std::endl;
  for (int i = 0; i <= iterations; ++i) {
    if (i % (iterations / 10) == 0) std::cout << i << std::endl;
    auto imports = wasm::vec<wasm::Extern*>::make();
    auto instance = wasm::Instance::make(store, module.get(), imports);
    if (!instance) {
      std::cout << "> Error instantiating module " << i << "!" << std::endl;
      exit(1);
    }
    instance->set_host_info(reinterpret_cast<void*>(i), &finalize);
    ++live_count;
  }

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();

  std::cout << "Live count " << live_count << std::endl;
  std::cout << "Creating store 1..." << std::endl;
  auto store1 = wasm::Store::make(engine.get());

  std::cout << "Running in store 1..." << std::endl;
  run_in_store(store1.get());
  std::cout << "Live count " << live_count << std::endl;

  {
    std::cout << "Creating store 2..." << std::endl;
    auto store2 = wasm::Store::make(engine.get());

    std::cout << "Running in store 2..." << std::endl;
    run_in_store(store2.get());
    std::cout << "Live count " << live_count << std::endl;

    std::cout << "Deleting store 2..." << std::endl;
    std::cout << "Live count " << live_count << std::endl;
  }

  std::cout << "Running in store 1..." << std::endl;
  run_in_store(store1.get());
  std::cout << "Live count " << live_count << std::endl;

  std::cout << "Deleting store 1..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Live count " << live_count << std::endl;
  assert(live_count == 0);
  std::cout << "Done." << std::endl;
  return 0;
}

