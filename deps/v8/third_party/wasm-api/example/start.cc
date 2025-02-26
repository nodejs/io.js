#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <cinttypes>

#include "wasm.hh"


void print_frame(const wasm::Frame* frame) {
  std::cout << "> " << frame->instance();
  std::cout << " @ 0x" << std::hex << frame->module_offset();
  std::cout << " = " << frame->func_index();
  std::cout << ".0x" << std::hex << frame->func_offset() << std::endl;
}


void run() {
  // Initialize.
  std::cout << "Initializing..." << std::endl;
  auto engine = wasm::Engine::make();
  auto store_ = wasm::Store::make(engine.get());
  auto store = store_.get();

  // Load binary.
  std::cout << "Loading binary..." << std::endl;
  std::ifstream file("start.wasm");
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
  wasm::own<wasm::Trap> trap;
  auto imports = wasm::vec<wasm::Extern*>::make();
  auto instance = wasm::Instance::make(store, module.get(), imports, &trap);
  if (instance || !trap) {
    std::cout << "> Error instantiating module, expected trap!" << std::endl;
    exit(1);
  }

  // Print result.
  std::cout << "Printing message..." << std::endl;
  std::cout << "> " << trap->message().get() << std::endl;

  std::cout << "Printing origin..." << std::endl;
  auto frame = trap->origin();
  if (frame) {
    print_frame(frame.get());
  } else {
    std::cout << "> Empty origin." << std::endl;
  }

  std::cout << "Printing trace..." << std::endl;
  auto trace = trap->trace();
  if (trace.size() > 0) {
    for (size_t i = 0; i < trace.size(); ++i) {
      print_frame(trace[i].get());
    }
  } else {
    std::cout << "> Empty trace." << std::endl;
  }

  // Shut down.
  std::cout << "Shutting down..." << std::endl;
}


int main(int argc, const char* argv[]) {
  run();
  std::cout << "Done." << std::endl;
  return 0;
}

