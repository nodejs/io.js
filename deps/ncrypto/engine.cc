#include "ncrypto.h"

namespace ncrypto {

// ============================================================================
// Engine

#ifndef OPENSSL_NO_ENGINE
EnginePointer::EnginePointer(ENGINE* engine_, bool finish_on_exit_)
    : engine(engine_), finish_on_exit(finish_on_exit_) {}

EnginePointer::EnginePointer(EnginePointer&& other) noexcept
    : engine(other.engine), finish_on_exit(other.finish_on_exit) {
  other.release();
}

EnginePointer::~EnginePointer() {
  reset();
}

EnginePointer& EnginePointer::operator=(EnginePointer&& other) noexcept {
  if (this == &other) return *this;
  this->~EnginePointer();
  return *new (this) EnginePointer(std::move(other));
}

void EnginePointer::reset(ENGINE* engine_, bool finish_on_exit_) {
  if (engine != nullptr) {
    if (finish_on_exit) {
      // This also does the equivalent of ENGINE_free.
      ENGINE_finish(engine);
    } else {
      ENGINE_free(engine);
    }
  }
  engine = engine_;
  finish_on_exit = finish_on_exit_;
}

ENGINE* EnginePointer::release() {
  ENGINE* ret = engine;
  engine = nullptr;
  finish_on_exit = false;
  return ret;
}

EnginePointer EnginePointer::getEngineByName(const std::string_view name,
                                             CryptoErrorList* errors) {
  MarkPopErrorOnReturn mark_pop_error_on_return(errors);
  EnginePointer engine(ENGINE_by_id(name.data()));
  if (!engine) {
    // Engine not found, try loading dynamically.
    engine = EnginePointer(ENGINE_by_id("dynamic"));
    if (engine) {
      if (!ENGINE_ctrl_cmd_string(engine.get(), "SO_PATH", name.data(), 0) ||
          !ENGINE_ctrl_cmd_string(engine.get(), "LOAD", nullptr, 0)) {
        engine.reset();
      }
    }
  }
  return engine;
}

bool EnginePointer::setAsDefault(uint32_t flags, CryptoErrorList* errors) {
  if (engine == nullptr) return false;
  ClearErrorOnReturn clear_error_on_return(errors);
  return ENGINE_set_default(engine, flags) != 0;
}

bool EnginePointer::init(bool finish_on_exit) {
  if (engine == nullptr) return false;
  if (finish_on_exit) setFinishOnExit();
  return ENGINE_init(engine) == 1;
}

EVPKeyPointer EnginePointer::loadPrivateKey(const std::string_view key_name) {
  if (engine == nullptr) return EVPKeyPointer();
  return EVPKeyPointer(
      ENGINE_load_private_key(engine, key_name.data(), nullptr, nullptr));
}

void EnginePointer::initEnginesOnce() {
  static bool initialized = false;
  if (!initialized) {
    ENGINE_load_builtin_engines();
    ENGINE_register_all_complete();
    initialized = true;
  }
}

#endif  // OPENSSL_NO_ENGINE

}  // namespace ncrypto
