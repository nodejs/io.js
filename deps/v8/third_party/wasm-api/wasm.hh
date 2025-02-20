// WebAssembly C++ API

#ifndef WASM_HH
#define WASM_HH

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <limits>
#include <string>

#ifndef WASM_API_EXTERN
#if defined(_WIN32) && !defined(__MINGW32__) && !defined(LIBWASM_STATIC)
#define WASM_API_EXTERN __declspec(dllimport)
#else
#define WASM_API_EXTERN
#endif
#endif


///////////////////////////////////////////////////////////////////////////////
// Auxiliaries

// Machine types

static_assert(sizeof(float) == sizeof(int32_t), "incompatible float type");
static_assert(sizeof(double) == sizeof(int64_t), "incompatible double type");
static_assert(sizeof(intptr_t) == sizeof(int32_t) ||
              sizeof(intptr_t) == sizeof(int64_t), "incompatible pointer type");

using byte_t = char;
using float32_t = float;
using float64_t = double;


namespace wasm {

// Vectors

template<class T>
class vec {
  static const size_t invalid_size = SIZE_MAX;

  size_t size_;
  std::unique_ptr<T[]> data_;

#ifdef WASM_API_DEBUG
  WASM_API_EXTERN void make_data();
  WASM_API_EXTERN void free_data();
#else
  void make_data() {}
  void free_data() {}
#endif

  vec(size_t size) : vec(size, size ? new(std::nothrow) T[size] : nullptr) {
    make_data();
  }

  vec(size_t size, T* data) : size_(size), data_(data) {
    assert(!!size_ == !!data_ || size_ == invalid_size);
  }

public:
  using elem_type = T;

  vec(vec<T>&& that) : vec(that.size_, that.data_.release()) {
    that.size_ = invalid_size;
  }

  ~vec() {
    free_data();
  }

  operator bool() const {
    return bool(size_ != invalid_size);
  }

  auto size() const -> size_t {
    return size_;
  }

  auto get() const -> const T* {
    return data_.get();
  }

  auto get() -> T* {
    return data_.get();
  }

  auto release() -> T* {
    size_ = invalid_size;
    return data_.release();
  }

  void reset() {
    free_data();
    size_ = invalid_size;
    data_.reset();
  }

  void reset(vec& that) {
    free_data();
    size_ = that.size_;
    data_.reset(that.data_.release());
    that.size_ = invalid_size;
  }

  auto operator=(vec&& that) -> vec& {
    reset(that);
    return *this;
  }

  auto operator[](size_t i) -> T& {
    assert(i < size_);
    return data_[i];
  }

  auto operator[](size_t i) const -> const T& {
    assert(i < size_);
    return data_[i];
  }

  auto copy() const -> vec {
    auto v = vec(size_);
    if (v) for (size_t i = 0; i < size_; ++i) v.data_[i] = data_[i];
    return v;
  }

  // TODO: This can't be used for e.g. vec<Val>
  auto deep_copy() const -> vec {
    auto v = vec(size_);
    if (v) for (size_t i = 0; i < size_; ++i) v.data_[i] = data_[i]->copy();
    return v;
  }

  static auto make_uninitialized(size_t size = 0) -> vec {
    return vec(size);
  }

  static auto make(size_t size, T init[]) -> vec {
    auto v = vec(size);
    if (v) for (size_t i = 0; i < size; ++i) v.data_[i] = std::move(init[i]);
    return v;
  }

  static auto make(std::string s) -> vec<char> {
    auto v = vec(s.length());
    if (v) std::strncpy(v.get(), s.data(), s.length());
    return v;
  }

  static auto make_nt(std::string s) -> vec<char> {
    auto v = vec(s.length() + 1);
    if (v) std::strcpy(v.get(), s.data());
    return v;
  }

  // TODO(mvsc): MVSC requires this special case:
  static auto make() -> vec {
    return vec(0);
  }

  template<class... Ts>
  static auto make(Ts&&... args) -> vec {
    T data[] = { std::forward<Ts>(args)... };
    return make(sizeof...(Ts), data);
  }

  static auto adopt(size_t size, T data[]) -> vec {
    return vec(size, data);
  }

  static auto invalid() -> vec {
    return vec(invalid_size, nullptr);
  }
};


// Ownership

class destroyer {
public:
  template <typename T>
  void operator()(T* ptr) {
    ptr->destroy();
  }
};

template<class T> using own = std::unique_ptr<T, destroyer>;
template<class T> using ownvec = vec<own<T>>;

template<class T> own<T> make_own(T* ptr) {
  return own<T>(ptr);
}

///////////////////////////////////////////////////////////////////////////////
// Runtime Environment

// Configuration

class WASM_API_EXTERN Config {
  friend class destroyer;
  void destroy();

protected:
  Config() = default;
  ~Config() = default;

public:
  static auto make() -> own<Config>;

  // Implementations may provide custom methods for manipulating Configs.
};


// Engine

class WASM_API_EXTERN Engine {
  friend class destroyer;
  void destroy();

protected:
  Engine() = default;
  ~Engine() = default;

public:
  static auto make(own<Config>&& = Config::make()) -> own<Engine>;
};


// Store

class WASM_API_EXTERN Store {
  friend class destroyer;
  void destroy();

protected:
  Store() = default;
  ~Store() = default;

public:
  static auto make(Engine*) -> own<Store>;
};


///////////////////////////////////////////////////////////////////////////////
// Type Representations

// Type attributes

enum class Mutability : uint8_t { CONST, VAR };

struct Limits {
  uint32_t min;
  uint32_t max;

  Limits(uint32_t min, uint32_t max = std::numeric_limits<uint32_t>::max()) :
    min(min), max(max) {}
};


// Value Types

enum class ValKind : uint8_t {
  I32, I64, F32, F64,
  EXTERNREF = 128, FUNCREF,
};

inline bool is_num(ValKind k) { return k < ValKind::EXTERNREF; }
inline bool is_ref(ValKind k) { return k >= ValKind::EXTERNREF; }


class WASM_API_EXTERN ValType {
  friend class destroyer;
  void destroy();

protected:
  ValType() = default;
  ~ValType() = default;

public:
  static auto make(ValKind) -> own<ValType>;
  auto copy() const -> own<ValType>;

  auto kind() const -> ValKind;
  auto is_num() const -> bool { return wasm::is_num(kind()); }
  auto is_ref() const -> bool { return wasm::is_ref(kind()); }
};


// External Types

enum class ExternKind : uint8_t {
  FUNC, GLOBAL, TABLE, MEMORY
};

class FuncType;
class GlobalType;
class TableType;
class MemoryType;

class WASM_API_EXTERN ExternType {
  friend class destroyer;
  void destroy();

protected:
  ExternType() = default;
  ~ExternType() = default;

public:
  auto copy() const-> own<ExternType>;

  auto kind() const -> ExternKind;

  auto func() -> FuncType*;
  auto global() -> GlobalType*;
  auto table() -> TableType*;
  auto memory() -> MemoryType*;

  auto func() const -> const FuncType*;
  auto global() const -> const GlobalType*;
  auto table() const -> const TableType*;
  auto memory() const -> const MemoryType*;
};


// Function Types

class WASM_API_EXTERN FuncType : public ExternType {
  friend class destroyer;
  void destroy();

protected:
  FuncType() = default;
  ~FuncType() = default;

public:
  static auto make(
    ownvec<ValType>&& params = ownvec<ValType>::make(),
    ownvec<ValType>&& results = ownvec<ValType>::make()
  ) -> own<FuncType>;

  auto copy() const -> own<FuncType>;

  auto params() const -> const ownvec<ValType>&;
  auto results() const -> const ownvec<ValType>&;
};


// Global Types

class WASM_API_EXTERN GlobalType : public ExternType {
  friend class destroyer;
  void destroy();

protected:
  GlobalType() = default;
  ~GlobalType() = default;

public:
  static auto make(own<ValType>&&, Mutability) -> own<GlobalType>;
  auto copy() const -> own<GlobalType>;

  auto content() const -> const ValType*;
  auto mutability() const -> Mutability;
};


// Table Types

class WASM_API_EXTERN TableType : public ExternType {
  friend class destroyer;
  void destroy();

protected:
  TableType() = default;
  ~TableType() = default;

public:
  static auto make(own<ValType>&&, Limits) -> own<TableType>;
  auto copy() const -> own<TableType>;

  auto element() const -> const ValType*;
  auto limits() const -> const Limits&;
};


// Memory Types

class WASM_API_EXTERN MemoryType : public ExternType {
  friend class destroyer;
  void destroy();

protected:
  MemoryType() = default;
  ~MemoryType() = default;

public:
  static auto make(Limits) -> own<MemoryType>;
  auto copy() const -> own<MemoryType>;

  auto limits() const -> const Limits&;
};


// Import Types

using Name = vec<byte_t>;

class WASM_API_EXTERN ImportType {
  friend class destroyer;
  void destroy();

protected:
  ImportType() = default;
  ~ImportType() = default;

public:
  static auto make(Name&& module, Name&& name, own<ExternType>&&) ->
    own<ImportType>;
  auto copy() const -> own<ImportType>;

  auto module() const -> const Name&;
  auto name() const -> const Name&;
  auto type() const -> const ExternType*;
};


// Export Types

class WASM_API_EXTERN ExportType {
  friend class destroyer;
  void destroy();

protected:
  ExportType() = default;
  ~ExportType() = default;

public:
  static auto make(Name&&, own<ExternType>&&) -> own<ExportType>;
  auto copy() const -> own<ExportType>;

  auto name() const -> const Name&;
  auto type() const -> const ExternType*;
};


///////////////////////////////////////////////////////////////////////////////
// Runtime Objects

// References

class WASM_API_EXTERN Ref {
  friend class destroyer;
  void destroy();

protected:
  Ref() = default;
  ~Ref() = default;

public:
  auto copy() const -> own<Ref>;
  auto same(const Ref*) const -> bool;

  auto get_host_info() const -> void*;
  void set_host_info(void* info, void (*finalizer)(void*) = nullptr);
};


// Values

class Val {
  ValKind kind_;
  union impl {
    int32_t i32;
    int64_t i64;
    float32_t f32;
    float64_t f64;
    Ref* ref;
  } impl_;

  Val(ValKind kind, impl impl) : kind_(kind), impl_(impl) {}

public:
  Val() : kind_(ValKind::EXTERNREF) { impl_.ref = nullptr; }
  explicit Val(int32_t i) : kind_(ValKind::I32) { impl_.i32 = i; }
  explicit Val(int64_t i) : kind_(ValKind::I64) { impl_.i64 = i; }
  explicit Val(float32_t z) : kind_(ValKind::F32) { impl_.f32 = z; }
  explicit Val(float64_t z) : kind_(ValKind::F64) { impl_.f64 = z; }
  explicit Val(own<Ref>&& r) : kind_(ValKind::EXTERNREF) { impl_.ref = r.release(); }

  Val(Val&& that) : kind_(that.kind_), impl_(that.impl_) {
    if (is_ref()) that.impl_.ref = nullptr;
  }

  ~Val() {
    reset();
  }

  auto is_num() const -> bool { return wasm::is_num(kind_); }
  auto is_ref() const -> bool { return wasm::is_ref(kind_); }

  static auto i32(int32_t x) -> Val { return Val(x); }
  static auto i64(int64_t x) -> Val { return Val(x); }
  static auto f32(float32_t x) -> Val { return Val(x); }
  static auto f64(float64_t x) -> Val { return Val(x); }
  static auto ref(own<Ref>&& x) -> Val { return Val(std::move(x)); }
  template<class T> inline static auto make(T x) -> Val;
  template<class T> inline static auto make(own<T>&& x) -> Val;

  void reset() {
    if (is_ref() && impl_.ref) {
      destroyer()(impl_.ref);
      impl_.ref = nullptr;
    }
  }

  void reset(Val& that) {
    reset();
    kind_ = that.kind_;
    impl_ = that.impl_;
    if (is_ref()) that.impl_.ref = nullptr;
  }

  auto operator=(Val&& that) -> Val& {
    reset(that);
    return *this;
  }

  auto kind() const -> ValKind { return kind_; }
  auto i32() const -> int32_t { assert(kind_ == ValKind::I32); return impl_.i32; }
  auto i64() const -> int64_t { assert(kind_ == ValKind::I64); return impl_.i64; }
  auto f32() const -> float32_t { assert(kind_ == ValKind::F32); return impl_.f32; }
  auto f64() const -> float64_t { assert(kind_ == ValKind::F64); return impl_.f64; }
  auto ref() const -> Ref* { assert(is_ref()); return impl_.ref; }
  template<class T> inline auto get() const -> T;

  auto release_ref() -> own<Ref> {
    assert(is_ref());
    auto ref = impl_.ref;
    impl_.ref = nullptr;
    return own<Ref>(ref);
  }

  auto copy() const -> Val {
    if (is_ref() && impl_.ref != nullptr) {
      // TODO(mvsc): MVSC cannot handle this:
      // impl impl = {.ref = impl_.ref->copy().release()};
      impl impl;
      impl.ref = impl_.ref->copy().release();
      return Val(kind_, impl);
    } else {
      return Val(kind_, impl_);
    }
  }
};


template<> inline auto Val::make<int32_t>(int32_t x) -> Val { return Val(x); }
template<> inline auto Val::make<int64_t>(int64_t x) -> Val { return Val(x); }
template<> inline auto Val::make<float32_t>(float32_t x) -> Val { return Val(x); }
template<> inline auto Val::make<float64_t>(float64_t x) -> Val { return Val(x); }
template<> inline auto Val::make<Ref>(own<Ref>&& x) -> Val {
  return Val(std::move(x));
}

template<> inline auto Val::make<uint32_t>(uint32_t x) -> Val {
  return Val(static_cast<int32_t>(x));
}
template<> inline auto Val::make<uint64_t>(uint64_t x) -> Val {
  return Val(static_cast<int64_t>(x));
}

template<> inline auto Val::get<int32_t>() const -> int32_t { return i32(); }
template<> inline auto Val::get<int64_t>() const -> int64_t { return i64(); }
template<> inline auto Val::get<float32_t>() const -> float32_t { return f32(); }
template<> inline auto Val::get<float64_t>() const -> float64_t { return f64(); }
template<> inline auto Val::get<Ref*>() const -> Ref* { return ref(); }

template<> inline auto Val::get<uint32_t>() const -> uint32_t {
  return static_cast<uint32_t>(i32());
}
template<> inline auto Val::get<uint64_t>() const -> uint64_t {
  return static_cast<uint64_t>(i64());
}


// Traps

using Message = vec<byte_t>;  // null terminated

class Instance;

class WASM_API_EXTERN Frame {
  friend class destroyer;
  void destroy();

protected:
  Frame() = default;
  ~Frame() = default;

public:
  auto copy() const -> own<Frame>;

  auto instance() const -> Instance*;
  auto func_index() const -> uint32_t;
  auto func_offset() const -> size_t;
  auto module_offset() const -> size_t;
};

class WASM_API_EXTERN Trap : public Ref {
  friend class destroyer;
  void destroy();

protected:
  Trap() = default;
  ~Trap() = default;

public:
  static auto make(Store*, const Message& msg) -> own<Trap>;
  auto copy() const -> own<Trap>;

  auto message() const -> Message;
  auto origin() const -> own<Frame>;  // may be null
  auto trace() const -> ownvec<Frame>;  // may be empty, origin first
};


// Modules

template<class T> class WASM_API_EXTERN Shared;

class WASM_API_EXTERN Module : public Ref {
  friend class destroyer;
  void destroy();

protected:
  Module() = default;
  ~Module() = default;

public:
  static auto validate(Store*, const vec<byte_t>& binary) -> bool;
  static auto make(Store*, const vec<byte_t>& binary) -> own<Module>;
  auto copy() const -> own<Module>;

  auto imports() const -> ownvec<ImportType>;
  auto exports() const -> ownvec<ExportType>;

  auto share() const -> own<Shared<Module>>;
  static auto obtain(Store*, const Shared<Module>*) -> own<Module>;

  auto serialize() const -> vec<byte_t>;
  static auto deserialize(Store*, const vec<byte_t>&) -> own<Module>;
};


// Shared objects

template<>
class WASM_API_EXTERN Shared<Module> {
  friend class destroyer;
  void destroy();

protected:
  Shared() = default;
  ~Shared() = default;
};


// Foreign Objects

class WASM_API_EXTERN Foreign : public Ref {
  friend class destroyer;
  void destroy();

protected:
  Foreign() = default;
  ~Foreign() = default;

public:
  static auto make(Store*) -> own<Foreign>;
  auto copy() const -> own<Foreign>;
};


// Externals

class Func;
class Global;
class Table;
class Memory;

class WASM_API_EXTERN Extern : public Ref {
  friend class destroyer;
  void destroy();

protected:
  Extern() = default;
  ~Extern() = default;

public:
  auto copy() const -> own<Extern>;

  auto kind() const -> ExternKind;
  auto type() const -> own<ExternType>;

  auto func() -> Func*;
  auto global() -> Global*;
  auto table() -> Table*;
  auto memory() -> Memory*;

  auto func() const -> const Func*;
  auto global() const -> const Global*;
  auto table() const -> const Table*;
  auto memory() const -> const Memory*;
};


// Function Instances

class WASM_API_EXTERN Func : public Extern {
  friend class destroyer;
  void destroy();

protected:
  Func() = default;
  ~Func() = default;

public:
  using callback = auto (*)(const vec<Val>&, vec<Val>&) -> own<Trap>;
  using callback_with_env = auto (*)(void*, const vec<Val>&, vec<Val>&) -> own<Trap>;

  static auto make(Store*, const FuncType*, callback) -> own<Func>;
  static auto make(Store*, const FuncType*, callback_with_env,
    void*, void (*finalizer)(void*) = nullptr) -> own<Func>;
  auto copy() const -> own<Func>;

  auto type() const -> own<FuncType>;
  auto param_arity() const -> size_t;
  auto result_arity() const -> size_t;

  auto call(const vec<Val>&, vec<Val>&) const -> own<Trap>;
};


// Global Instances

class WASM_API_EXTERN Global : public Extern {
  friend class destroyer;
  void destroy();

protected:
  Global() = default;
  ~Global() = default;

 public:
  static auto make(Store*, const GlobalType*, const Val&) -> own<Global>;
  auto copy() const -> own<Global>;

  auto type() const -> own<GlobalType>;
  auto get() const -> Val;
  void set(const Val&);
};


// Table Instances

class WASM_API_EXTERN Table : public Extern {
  friend class destroyer;
  void destroy();

protected:
  Table() = default;
  ~Table() = default;

public:
  using size_t = uint32_t;

  static auto make(
    Store*, const TableType*, const Ref* init = nullptr) -> own<Table>;
  auto copy() const -> own<Table>;

  auto type() const -> own<TableType>;
  auto get(size_t index) const -> own<Ref>;
  auto set(size_t index, const Ref*) -> bool;
  auto size() const -> size_t;
  auto grow(size_t delta, const Ref* init = nullptr) -> bool;
};


// Memory Instances

class WASM_API_EXTERN Memory : public Extern {
  friend class destroyer;
  void destroy();

protected:
  Memory() = default;
  ~Memory() = default;

public:
  static auto make(Store*, const MemoryType*) -> own<Memory>;
  auto copy() const -> own<Memory>;

  using pages_t = uint32_t;

  static const size_t page_size = 0x10000;

  auto type() const -> own<MemoryType>;
  auto data() const -> byte_t*;
  auto data_size() const -> size_t;
  auto size() const -> pages_t;
  auto grow(pages_t delta) -> bool;
};


// Module Instances

class WASM_API_EXTERN Instance : public Ref {
  friend class destroyer;
  void destroy();

protected:
  Instance() = default;
  ~Instance() = default;

public:
  static auto make(
    Store*, const Module*, const vec<Extern*>&, own<Trap>* = nullptr
  ) -> own<Instance>;
  auto copy() const -> own<Instance>;

  auto exports() const -> ownvec<Extern>;
};


///////////////////////////////////////////////////////////////////////////////

}  // namespace wasm

#endif  // #ifdef WASM_HH
