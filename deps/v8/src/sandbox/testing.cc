// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/testing.h"

#include "src/api/api-inl.h"
#include "src/api/api-natives.h"
#include "src/common/globals.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/objects/backing-store.h"
#include "src/objects/js-objects.h"
#include "src/objects/templates.h"
#include "src/sandbox/sandbox.h"

#ifdef V8_OS_LINUX
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif  // V8_OS_LINUX

#ifdef V8_USE_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif  // V8_USE_ADDRESS_SANITIZER

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

SandboxTesting::Mode SandboxTesting::mode_ = SandboxTesting::Mode::kDisabled;
Address SandboxTesting::target_page_base_ = kNullAddress;
Address SandboxTesting::target_page_size_ = 0;

#ifdef V8_ENABLE_MEMORY_CORRUPTION_API

namespace {

// Sandbox.base
void SandboxGetBase(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  double sandbox_base = GetProcessWideSandbox()->base();
  info.GetReturnValue().Set(v8::Number::New(isolate, sandbox_base));
}
// Sandbox.byteLength
void SandboxGetByteLength(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  double sandbox_size = GetProcessWideSandbox()->size();
  info.GetReturnValue().Set(v8::Number::New(isolate, sandbox_size));
}

// new Sandbox.MemoryView(info) -> Sandbox.MemoryView
void SandboxMemoryView(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  Local<v8::Context> context = isolate->GetCurrentContext();

  if (!info.IsConstructCall()) {
    isolate->ThrowError("Sandbox.MemoryView must be invoked with 'new'");
    return;
  }

  Local<v8::Integer> arg1, arg2;
  if (!info[0]->ToInteger(context).ToLocal(&arg1) ||
      !info[1]->ToInteger(context).ToLocal(&arg2)) {
    isolate->ThrowError("Expects two number arguments (start offset and size)");
    return;
  }

  Sandbox* sandbox = GetProcessWideSandbox();
  CHECK_LE(sandbox->size(), kMaxSafeIntegerUint64);

  uint64_t offset = arg1->Value();
  uint64_t size = arg2->Value();
  if (offset > sandbox->size() || size > sandbox->size() ||
      (offset + size) > sandbox->size()) {
    isolate->ThrowError(
        "The MemoryView must be entirely contained within the sandbox");
    return;
  }

  Factory* factory = reinterpret_cast<Isolate*>(isolate)->factory();
  std::unique_ptr<BackingStore> memory = BackingStore::WrapAllocation(
      reinterpret_cast<void*>(sandbox->base() + offset), size,
      v8::BackingStore::EmptyDeleter, nullptr, SharedFlag::kNotShared);
  if (!memory) {
    isolate->ThrowError("Out of memory: MemoryView backing store");
    return;
  }
  Handle<JSArrayBuffer> buffer = factory->NewJSArrayBuffer(std::move(memory));
  info.GetReturnValue().Set(Utils::ToLocal(buffer));
}

// The methods below either take a HeapObject or the address of a HeapObject as
// argument. These helper functions can be used to extract the argument object
// in both cases.
using ArgumentObjectExtractorFunction = std::function<bool(
    const v8::FunctionCallbackInfo<v8::Value>&, Tagged<HeapObject>* out)>;

static bool GetArgumentObjectPassedAsReference(
    const v8::FunctionCallbackInfo<v8::Value>& info, Tagged<HeapObject>* out) {
  v8::Isolate* isolate = info.GetIsolate();

  if (info.Length() == 0) {
    isolate->ThrowError("First argument must be provided");
    return false;
  }

  Handle<Object> arg = Utils::OpenHandle(*info[0]);
  if (!IsHeapObject(*arg)) {
    isolate->ThrowError("First argument must be a HeapObject");
    return false;
  }

  *out = Cast<HeapObject>(*arg);
  return true;
}

static bool GetArgumentObjectPassedAsAddress(
    const v8::FunctionCallbackInfo<v8::Value>& info, Tagged<HeapObject>* out) {
  Sandbox* sandbox = GetProcessWideSandbox();
  v8::Isolate* isolate = info.GetIsolate();
  Local<v8::Context> context = isolate->GetCurrentContext();

  if (info.Length() == 0) {
    isolate->ThrowError("First argument must be provided");
    return false;
  }

  Local<v8::Uint32> arg1;
  if (!info[0]->ToUint32(context).ToLocal(&arg1)) {
    isolate->ThrowError("First argument must be the address of a HeapObject");
    return false;
  }

  uint32_t address = arg1->Value();
  // Allow tagged addresses by removing the kHeapObjectTag and
  // kWeakHeapObjectTag. This allows clients to just read tagged pointers from
  // the heap and use them for these APIs.
  address &= ~kHeapObjectTagMask;
  *out = HeapObject::FromAddress(sandbox->base() + address);
  return true;
}

// Sandbox.getAddressOf(Object) -> Number
void SandboxGetAddressOf(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  Tagged<HeapObject> obj;
  if (!GetArgumentObjectPassedAsReference(info, &obj)) {
    return;
  }

  // HeapObjects must be allocated inside the pointer compression cage so their
  // address relative to the start of the sandbox can be obtained simply by
  // taking the lowest 32 bits of the absolute address.
  uint32_t address = static_cast<uint32_t>(obj->address());
  info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(isolate, address));
}

// Sandbox.getObjectAt(Number) -> Object
void SandboxGetObjectAt(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  Tagged<HeapObject> obj;
  if (!GetArgumentObjectPassedAsAddress(info, &obj)) {
    return;
  }

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  Handle<Object> handle(obj, i_isolate);
  info.GetReturnValue().Set(ToApiHandle<v8::Value>(handle));
}

// Sandbox.isValidObjectAt(Address) -> Bool
void SandboxIsValidObjectAt(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  Tagged<HeapObject> obj;
  if (!GetArgumentObjectPassedAsAddress(info, &obj)) {
    return;
  }

  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  auto IsLocatedInMappedMemory = [&](Tagged<HeapObject> obj) {
    // Note that IsOutsideAllocatedSpace is imprecise and may return false for
    // some addresses outside the allocated space. However, it's probably good
    // enough for our purposes.
    return !heap->memory_allocator()->IsOutsideAllocatedSpace(obj.address());
  };

  bool is_valid = false;
  if (IsLocatedInMappedMemory(obj)) {
    Tagged<Map> map = obj->map();
    if (IsLocatedInMappedMemory(map)) {
      is_valid = IsMap(map);
    }
  }

  info.GetReturnValue().Set(is_valid);
}

static void SandboxIsWritableImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    ArgumentObjectExtractorFunction getArgumentObject) {
  DCHECK(ValidateCallbackInfo(info));

  Tagged<HeapObject> obj;
  if (!getArgumentObject(info, &obj)) {
    return;
  }

  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromHeapObject(obj);
  bool is_writable = chunk->IsWritable();
  info.GetReturnValue().Set(is_writable);
}

// Sandbox.isWritable(Object) -> Bool
void SandboxIsWritable(const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxIsWritableImpl(info, &GetArgumentObjectPassedAsReference);
}

// Sandbox.isWritableObjectAt(Number) -> Bool
void SandboxIsWritableObjectAt(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxIsWritableImpl(info, &GetArgumentObjectPassedAsAddress);
}

static void SandboxGetSizeOfImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    ArgumentObjectExtractorFunction getArgumentObject) {
  DCHECK(ValidateCallbackInfo(info));

  Tagged<HeapObject> obj;
  if (!getArgumentObject(info, &obj)) {
    return;
  }

  int size = obj->Size();
  info.GetReturnValue().Set(size);
}

// Sandbox.getSizeOf(Object) -> Number
void SandboxGetSizeOf(const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetSizeOfImpl(info, &GetArgumentObjectPassedAsReference);
}

// Sandbox.getSizeOfObjectAt(Number) -> Number
void SandboxGetSizeOfObjectAt(const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetSizeOfImpl(info, &GetArgumentObjectPassedAsAddress);
}

static void SandboxGetInstanceTypeOfImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    ArgumentObjectExtractorFunction getArgumentObject) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  Tagged<HeapObject> obj;
  if (!getArgumentObject(info, &obj)) {
    return;
  }

  InstanceType type = obj->map()->instance_type();
  std::stringstream out;
  out << type;
  MaybeLocal<v8::String> result =
      v8::String::NewFromUtf8(isolate, out.str().c_str());
  info.GetReturnValue().Set(result.ToLocalChecked());
}

// Sandbox.getInstanceTypeOf(Object) -> String
void SandboxGetInstanceTypeOf(const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetInstanceTypeOfImpl(info, &GetArgumentObjectPassedAsReference);
}

// Sandbox.getInstanceTypeOfObjectAt(Number) -> String
void SandboxGetInstanceTypeOfObjectAt(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetInstanceTypeOfImpl(info, &GetArgumentObjectPassedAsAddress);
}

static void SandboxGetInstanceTypeIdOfImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    ArgumentObjectExtractorFunction getArgumentObject) {
  DCHECK(ValidateCallbackInfo(info));

  Tagged<HeapObject> obj;
  if (!getArgumentObject(info, &obj)) {
    return;
  }

  InstanceType type = obj->map()->instance_type();
  static_assert(std::is_same_v<std::underlying_type_t<InstanceType>, uint16_t>);
  if (type > LAST_TYPE) {
    // This can happen with corrupted objects. Canonicalize to a special
    // "unknown" instance type to indicate that this is an unknown type.
    const uint16_t kUnknownInstanceType = std::numeric_limits<uint16_t>::max();
    type = static_cast<InstanceType>(kUnknownInstanceType);
  }

  info.GetReturnValue().Set(type);
}

// Sandbox.getInstanceTypeIdOf(Object) -> Number
void SandboxGetInstanceTypeIdOf(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetInstanceTypeIdOfImpl(info, &GetArgumentObjectPassedAsReference);
}

// Sandbox.getInstanceTypeIdOfObjectAt(Number) -> Number
void SandboxGetInstanceTypeIdOfObjectAt(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetInstanceTypeIdOfImpl(info, &GetArgumentObjectPassedAsAddress);
}

// Obtain the offset of a field in an object.
//
// This can be used to obtain the offsets of internal object fields in order to
// avoid hardcoding offsets into testcases. It basically makes the various
// Foo::kBarOffset constants accessible from JavaScript. The main benefit of
// that is that testcases continue to work if the field offset changes.
// Additionally, if a field is removed, testcases that use it will fail and can
// then be deleted if they are no longer useful.
void SandboxGetFieldOffsetImpl(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    ArgumentObjectExtractorFunction getArgumentObject) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  Tagged<HeapObject> obj;
  if (!getArgumentObject(info, &obj)) {
    return;
  }
  InstanceType instance_type = Cast<HeapObject>(*obj)->map()->instance_type();

  v8::String::Utf8Value field_name(isolate, info[1]);
  if (!*field_name) {
    isolate->ThrowError("Second argument must be a string");
    return;
  }

  auto& all_fields = SandboxTesting::GetFieldOffsetMap();
  if (all_fields.find(instance_type) == all_fields.end()) {
    isolate->ThrowError(
        "Unknown object type. If needed, add it in "
        "SandboxTesting::GetFieldOffsetMap");
    return;
  }

  auto& obj_fields = all_fields[instance_type];
  if (obj_fields.find(*field_name) == obj_fields.end()) {
    isolate->ThrowError(
        "Unknown field. If needed, add it in "
        "SandboxTesting::GetFieldOffsetMap");
    return;
  }

  int offset = obj_fields[*field_name];
  info.GetReturnValue().Set(offset);
}

// Sandbox.getFieldOffsetOf(Object, String) -> Number
void SandboxGetFieldOffsetOf(const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetFieldOffsetImpl(info, &GetArgumentObjectPassedAsReference);
}

// Sandbox.getFieldOffsetOfObjectAt(Number, String) -> Number
void SandboxGetFieldOffsetOfObjectAt(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  SandboxGetFieldOffsetImpl(info, &GetArgumentObjectPassedAsAddress);
}

// Sandbox.targetPage
void SandboxGetTargetPage(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  Address page = SandboxTesting::target_page_base();
  CHECK_NE(page, kNullAddress);
  info.GetReturnValue().Set(v8::Number::New(isolate, page));
}

Handle<FunctionTemplateInfo> NewFunctionTemplate(
    Isolate* isolate, FunctionCallback func,
    ConstructorBehavior constructor_behavior) {
  // Use the API functions here as they are more convenient to use.
  v8::Isolate* api_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  Local<FunctionTemplate> function_template =
      FunctionTemplate::New(api_isolate, func, {}, {}, 0, constructor_behavior,
                            SideEffectType::kHasSideEffect);
  return v8::Utils::OpenHandle(*function_template);
}

Handle<JSFunction> CreateFunc(Isolate* isolate, FunctionCallback func,
                              Handle<String> name, bool is_constructor) {
  ConstructorBehavior constructor_behavior = is_constructor
                                                 ? ConstructorBehavior::kAllow
                                                 : ConstructorBehavior::kThrow;
  Handle<FunctionTemplateInfo> function_template =
      NewFunctionTemplate(isolate, func, constructor_behavior);
  return ApiNatives::InstantiateFunction(isolate, function_template, name)
      .ToHandleChecked();
}

void InstallFunc(Isolate* isolate, Handle<JSObject> holder,
                 FunctionCallback func, const char* name, int num_parameters,
                 bool is_constructor) {
  Factory* factory = isolate->factory();
  Handle<String> function_name = factory->NewStringFromAsciiChecked(name);
  Handle<JSFunction> function =
      CreateFunc(isolate, func, function_name, is_constructor);
  function->shared()->set_length(num_parameters);
  JSObject::AddProperty(isolate, holder, function_name, function, NONE);
}

void InstallGetter(Isolate* isolate, Handle<JSObject> object,
                   FunctionCallback func, const char* name) {
  Factory* factory = isolate->factory();
  Handle<String> property_name = factory->NewStringFromAsciiChecked(name);
  Handle<JSFunction> getter = CreateFunc(isolate, func, property_name, false);
  Handle<Object> setter = factory->null_value();
  JSObject::DefineOwnAccessorIgnoreAttributes(object, property_name, getter,
                                              setter, FROZEN);
}

void InstallFunction(Isolate* isolate, Handle<JSObject> holder,
                     FunctionCallback func, const char* name,
                     int num_parameters) {
  InstallFunc(isolate, holder, func, name, num_parameters, false);
}

void InstallConstructor(Isolate* isolate, Handle<JSObject> holder,
                        FunctionCallback func, const char* name,
                        int num_parameters) {
  InstallFunc(isolate, holder, func, name, num_parameters, true);
}
}  // namespace

void SandboxTesting::InstallMemoryCorruptionApiIfEnabled(Isolate* isolate) {
#ifndef V8_ENABLE_MEMORY_CORRUPTION_API
#error "This function should not be available in any shipping build "          \
       "where it could potentially be abused to facilitate exploitation."
#endif

  if (!IsEnabled()) return;

  CHECK(GetProcessWideSandbox()->is_initialized());

  // Create the special Sandbox object that provides read/write access to the
  // sandbox address space alongside other miscellaneous functionality.
  Handle<JSObject> sandbox = isolate->factory()->NewJSObject(
      isolate->object_function(), AllocationType::kOld);

  InstallGetter(isolate, sandbox, SandboxGetBase, "base");
  InstallGetter(isolate, sandbox, SandboxGetByteLength, "byteLength");
  InstallConstructor(isolate, sandbox, SandboxMemoryView, "MemoryView", 2);
  InstallFunction(isolate, sandbox, SandboxGetAddressOf, "getAddressOf", 1);
  InstallFunction(isolate, sandbox, SandboxGetObjectAt, "getObjectAt", 1);
  InstallFunction(isolate, sandbox, SandboxIsValidObjectAt, "isValidObjectAt",
                  1);
  InstallFunction(isolate, sandbox, SandboxIsWritable, "isWritable", 1);
  InstallFunction(isolate, sandbox, SandboxIsWritableObjectAt,
                  "isWritableObjectAt", 1);
  InstallFunction(isolate, sandbox, SandboxGetSizeOf, "getSizeOf", 1);
  InstallFunction(isolate, sandbox, SandboxGetSizeOfObjectAt,
                  "getSizeOfObjectAt", 1);
  InstallFunction(isolate, sandbox, SandboxGetInstanceTypeOf,
                  "getInstanceTypeOf", 1);
  InstallFunction(isolate, sandbox, SandboxGetInstanceTypeOfObjectAt,
                  "getInstanceTypeOfObjectAt", 1);
  InstallFunction(isolate, sandbox, SandboxGetInstanceTypeIdOf,
                  "getInstanceTypeIdOf", 1);
  InstallFunction(isolate, sandbox, SandboxGetInstanceTypeIdOfObjectAt,
                  "getInstanceTypeIdOfObjectAt", 1);
  InstallFunction(isolate, sandbox, SandboxGetFieldOffsetOf, "getFieldOffsetOf",
                  2);
  InstallFunction(isolate, sandbox, SandboxGetFieldOffsetOfObjectAt,
                  "getFieldOffsetOfObjectAt", 2);

  if (mode() == Mode::kForTesting) {
    InstallGetter(isolate, sandbox, SandboxGetTargetPage, "targetPage");
  }

  // Install the Sandbox object as property on the global object.
  Handle<JSGlobalObject> global = isolate->global_object();
  Handle<String> name =
      isolate->factory()->NewStringFromAsciiChecked("Sandbox");
  JSObject::AddProperty(isolate, global, name, sandbox, DONT_ENUM);
}

#endif  // V8_ENABLE_MEMORY_CORRUPTION_API

namespace {
#ifdef V8_OS_LINUX

void PrintToStderr(const char* output) {
  // NOTE: This code MUST be async-signal safe.
  // NO malloc or stdio is allowed here.
  ssize_t return_val = write(STDERR_FILENO, output, strlen(output));
  USE(return_val);
}

[[noreturn]] void FilterCrash(const char* reason) {
  // NOTE: This code MUST be async-signal safe.
  // NO malloc or stdio is allowed here.
  PrintToStderr(reason);
  // In sandbox fuzzing mode, we want to exit with a non-zero status to
  // indicate to the fuzzer that the sample "failed" (ran into an unrecoverable
  // error) and should probably not be mutated further. Otherwise, we exit with
  // zero, which is for example needed for regression tests to make them "pass"
  // when no sandbox violation is detected.
  int status =
      SandboxTesting::mode() == SandboxTesting::Mode::kForFuzzing ? -1 : 0;
  _exit(status);
}

// Signal handler checking whether a memory access violation happened inside or
// outside of the sandbox address space. If inside, the signal is ignored and
// the process terminated normally, in the latter case the original signal
// handler is restored and the signal delivered again.
struct sigaction g_old_sigabrt_handler, g_old_sigtrap_handler,
    g_old_sigbus_handler, g_old_sigsegv_handler;

void UninstallCrashFilter() {
  // NOTE: This code MUST be async-signal safe.
  // NO malloc or stdio is allowed here.

  // It's important that we always restore all signal handlers. For example, if
  // we forward a SIGSEGV to Asan's signal handler, that signal handler may
  // terminate the process with SIGABRT, which we must then *not* ignore.
  //
  // Should any of the sigaction calls below ever fail, the default signal
  // handler will be invoked (due to SA_RESETHAND) and will terminate the
  // process, so there's no need to attempt to handle that condition.
  sigaction(SIGABRT, &g_old_sigabrt_handler, nullptr);
  sigaction(SIGTRAP, &g_old_sigtrap_handler, nullptr);
  sigaction(SIGBUS, &g_old_sigbus_handler, nullptr);
  sigaction(SIGSEGV, &g_old_sigsegv_handler, nullptr);

  // We should also uninstall the sanitizer death callback as our crash filter
  // may hand a crash over to ASan, which should then not enter our crash
  // filtering logic a second time.
#ifdef V8_USE_ADDRESS_SANITIZER
  __sanitizer_set_death_callback(nullptr);
#endif
}

void CrashFilter(int signal, siginfo_t* info, void* void_context) {
  // NOTE: This code MUST be async-signal safe.
  // NO malloc or stdio is allowed here.

  if (signal == SIGABRT) {
    // SIGABRT typically indicates a failed CHECK or similar, which is harmless.
    FilterCrash("Caught harmless signal (SIGABRT). Exiting process...\n");
  }

  if (signal == SIGTRAP) {
    // Similarly, SIGTRAP may for example indicate UNREACHABLE code.
    FilterCrash("Caught harmless signal (SIGTRAP). Exiting process...\n");
  }

  Address faultaddr = reinterpret_cast<Address>(info->si_addr);

  if (GetProcessWideSandbox()->Contains(faultaddr)) {
    FilterCrash(
        "Caught harmless memory access violaton (inside sandbox address "
        "space). Exiting process...\n");
  }

  if (info->si_code == SI_KERNEL && faultaddr == 0) {
    // This combination appears to indicate a crash at a non-canonical address
    // on Linux. Crashes at non-canonical addresses are for example caused by
    // failed external pointer type checks. Memory accesses that _always_ land
    // at a non-canonical address are not exploitable and so these are filtered
    // out here. However, testcases need to be written with this in mind and
    // must cause crashes at valid addresses.
    FilterCrash(
        "Caught harmless memory access violaton (non-canonical address). "
        "Exiting process...\n");
  }

  if (faultaddr >= 0x8000'0000'0000'0000ULL) {
    // On Linux, it appears that the kernel will still report valid (i.e.
    // canonical) kernel space addresses via the si_addr field, so we need to
    // handle these separately. We've already filtered out non-canonical
    // addresses above, so here we can just test if the most-significant bit of
    // the address is set, and if so assume that it's a kernel address.
    FilterCrash(
        "Caught harmless memory access violatation (kernel space address). "
        "Exiting process...\n");
  }

  if (faultaddr < 0x1000) {
    // Nullptr dereferences are harmless as nothing can be mapped there. We use
    // the typical page size (which is also the default value of mmap_min_addr
    // on Linux) to determine what counts as a nullptr dereference here.
    FilterCrash(
        "Caught harmless memory access violaton (nullptr dereference). Exiting "
        "process...\n");
  }

  if (faultaddr < 4ULL * GB) {
    // Currently we also ignore access violations in the first 4GB of the
    // virtual address space. See crbug.com/1470641 for more details.
    FilterCrash(
        "Caught harmless memory access violaton (first 4GB of virtual address "
        "space). Exiting process...\n");
  }

  // Stack overflow detection.
  //
  // On Linux, we generally have two types of stacks:
  //  1. The main thread's stack, allocated by the kernel, and
  //  2. The stacks of any other thread, allocated by the application
  //
  // These stacks differ in some ways, and that affects the way stack overflows
  // (caused e.g. by unbounded recursion) materialize: for (1) the kernel will
  // use a "gap" region below the stack segment, i.e. an unmapped area into
  // which the kernel itself will not place any mappings and into which the
  // stack cannot grow. A stack overflow therefore crashes with a SEGV_MAPERR.
  // On the other hand, for (2) the application is responsible for allocating
  // the stack and therefore also for allocating any guard regions around it.
  // As these guard regions must be regular mappings (with PROT_NONE), a stack
  // overflow will crash with a SEGV_ACCERR.
  //
  // It's relatively hard to reliably and accurately detect stack overflow, so
  // here we use a simple heuristic: did we crash on any kind of access
  // violation on an address just below the current thread's stack region. This
  // may cause both false positives (e.g. an access not through the stack
  // pointer register that happens to also land just below the stack) and false
  // negatives (e.g. a stack overflow on the main thread that "jumps over" the
  // first page of the gap region), but is probably good enough in practice.
  pthread_attr_t attr;
  int pthread_error = pthread_getattr_np(pthread_self(), &attr);
  if (!pthread_error) {
    uintptr_t stack_base;
    size_t stack_size;
    pthread_error = pthread_attr_getstack(
        &attr, reinterpret_cast<void**>(&stack_base), &stack_size);
    // The main thread's stack on Linux typically has a fairly large gap region
    // (1MB by default), but other thread's stacks usually have smaller guard
    // regions so here we're conservative and assume that the guard region
    // consists only of a single page.
    const size_t kMinStackGuardRegionSize = sysconf(_SC_PAGESIZE);
    uintptr_t stack_guard_region_start = stack_base - kMinStackGuardRegionSize;
    uintptr_t stack_guard_region_end = stack_base;
    if (!pthread_error && stack_guard_region_start <= faultaddr &&
        faultaddr < stack_guard_region_end) {
      FilterCrash("Caught harmless stack overflow. Exiting process...\n");
    }
  }

  if (info->si_code == SEGV_ACCERR &&
      !SandboxTesting::IsInsideTargetPage(faultaddr)) {
    // This indicates an access to a valid mapping but with insufficient
    // permissions, for example accessing a region mapped with PROT_NONE, or
    // writing to a read-only mapping.
    //
    // The sandbox relies on such accesses crashing in a safe way in some
    // cases. For example, the accesses into the various pointer tables are not
    // bounds checked, but instead it is guaranteed that an out-of-bounds
    // access will hit a PROT_NONE mapping.
    //
    // Memory accesses that _always_ cause such a permission violation are not
    // exploitable and the crashes are therefore filtered out here. However,
    // testcases need to be written with this behavior in mind and should
    // typically try to access non-existing memory to demonstrate the ability
    // to escape from the sandbox.
    //
    // An exception to this rule is the target page: in sandbox testing mode a
    // write access to this read-only page is considered a sandbox bypass.
    FilterCrash(
        "Caught harmless memory access violaton (memory permission violation). "
        "Exiting process...\n");
  }

  if (SandboxTesting::mode() == SandboxTesting::Mode::kForTesting) {
    // In sandbox testing mode, the access violation must happen on a specific
    // page and it must be a write access. Check for this now.
    // As the target page is mapped as read-only, a SEGV will only be raised
    // for a write (or execute) access, so we do not need to check for this.
    if (!SandboxTesting::IsInsideTargetPage(faultaddr)) {
      FilterCrash(
          "Detected invalid sandbox violation (not on target page). Exiting "
          "process...\n");
    }
  }

  // Otherwise it's a sandbox violation, so restore the original signal
  // handlers, then return from this handler. The faulting instruction will be
  // re-executed and will again trigger the access violation, but now the
  // signal will be handled by the original signal handler.
  UninstallCrashFilter();

  PrintToStderr("\n## V8 sandbox violation detected!\n\n");
}

#ifdef V8_USE_ADDRESS_SANITIZER
void AsanFaultHandler() {
  Address faultaddr = reinterpret_cast<Address>(__asan_get_report_address());

  if (faultaddr == kNullAddress) {
    FilterCrash(
        "Caught ASan fault without a fault address. Ignoring it as we cannot "
        "check if it is a sandbox violation. Exiting process...\n");
  }

  if (GetProcessWideSandbox()->Contains(faultaddr)) {
    FilterCrash(
        "Caught harmless ASan fault (inside sandbox address space). Exiting "
        "process...\n");
  }

  // Asan may report the failure via abort(), so we should also restore the
  // original signal handlers here.
  UninstallCrashFilter();

  PrintToStderr("\n## V8 sandbox violation detected!\n\n");
}
#endif  // V8_USE_ADDRESS_SANITIZER

void InstallCrashFilter() {
  // Register an alternate stack for signal delivery so that signal handlers
  // can run properly even if for example the stack pointer has been corrupted
  // or the stack has overflowed.
  // Note that the alternate stack is currently only registered for the main
  // thread. Stack pointer corruption or stack overflows on background threads
  // may therefore still cause the signal handler to crash.
  VirtualAddressSpace* vas = GetPlatformVirtualAddressSpace();
  Address alternate_stack =
      vas->AllocatePages(VirtualAddressSpace::kNoHint, SIGSTKSZ,
                         vas->page_size(), PagePermissions::kReadWrite);
  CHECK_NE(alternate_stack, kNullAddress);
  stack_t signalstack = {
      .ss_sp = reinterpret_cast<void*>(alternate_stack),
      .ss_flags = 0,
      .ss_size = static_cast<size_t>(SIGSTKSZ),
  };
  CHECK_EQ(sigaltstack(&signalstack, nullptr), 0);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  action.sa_sigaction = &CrashFilter;
  sigemptyset(&action.sa_mask);

  bool success = true;
  success &= (sigaction(SIGABRT, &action, &g_old_sigabrt_handler) == 0);
  success &= (sigaction(SIGTRAP, &action, &g_old_sigtrap_handler) == 0);
  success &= (sigaction(SIGBUS, &action, &g_old_sigbus_handler) == 0);
  success &= (sigaction(SIGSEGV, &action, &g_old_sigsegv_handler) == 0);
  CHECK(success);

#if defined(V8_USE_ADDRESS_SANITIZER)
  __sanitizer_set_death_callback(&AsanFaultHandler);
#elif defined(V8_USE_MEMORY_SANITIZER) || \
    defined(V8_USE_UNDEFINED_BEHAVIOR_SANITIZER)
  // TODO(saelo): can we also test for the other sanitizers here somehow?
  FATAL("The sandbox crash filter currently only supports AddressSanitizer");
#endif
}

#endif  // V8_OS_LINUX
}  // namespace

void SandboxTesting::Enable(Mode mode) {
  CHECK_EQ(mode_, Mode::kDisabled);
  CHECK_NE(mode, Mode::kDisabled);
  CHECK(GetProcessWideSandbox()->is_initialized());

  mode_ = mode;

  if (mode == Mode::kForTesting) {
#ifdef V8_USE_ADDRESS_SANITIZER
    // This doesn't make sense: ASan would catch many bugs early on, before
    // they can be turned into an arbitrary write primitive.
    FATAL(
        "The sandbox testing mode is currently incompatible with "
        "AddressSanitizer");
#else
    // Map the target address that must be written to to demonstrate a sandbox
    // bypass. A simple way to enforce that the access is a write (or execute)
    // access is by mapping the page readable. That way, read accesses do not
    // cause a crash and so won't be seen by the crash filter at all.
    VirtualAddressSpace* vas = GetPlatformVirtualAddressSpace();
    target_page_size_ = vas->page_size();
    target_page_base_ =
        vas->AllocatePages(vas->RandomPageAddress(), target_page_size_,
                           target_page_size_, PagePermissions::kRead);
    CHECK_NE(target_page_base_, kNullAddress);
    fprintf(stderr,
            "Sandbox testing mode is enabled. Write to the page starting at "
            "0x%" V8PRIxPTR
            " (available from JavaScript as `Sandbox.targetPage`) to "
            "demonstrate a sandbox bypass.\n",
            target_page_base_);
#endif  // V8_USE_ADDRESS_SANITIZER
  } else {
    fprintf(stderr,
            "Sandbox fuzzing mode is enabled. Only sandbox violations will be "
            "reported, all other crashes will be ignored.\n");
  }

#ifdef V8_OS_LINUX
  InstallCrashFilter();
#else
  FATAL("The sandbox crash filter is currently only available on Linux");
#endif  // V8_OS_LINUX
}

bool SandboxTesting::IsInsideTargetPage(Address faultaddr) {
  return base::IsInHalfOpenRange(faultaddr, target_page_base(),
                                 target_page_base() + target_page_size());
}

SandboxTesting::FieldOffsetMap& SandboxTesting::GetFieldOffsetMap() {
  // This mechanism is currently very crude and needs to be manually maintained
  // and extended (e.g. when adding a js test for the sandbox). In the future,
  // it would be nice to somehow automatically generate this map from the
  // object definitions and also support the class inheritance hierarchy.
  static base::LeakyObject<FieldOffsetMap> g_known_fields;
  auto& fields = *g_known_fields.get();
  bool is_initialized = fields.size() != 0;
  if (!is_initialized) {
    fields[JS_ARRAY_TYPE]["length"] = JSArray::kLengthOffset;
    fields[SLICED_ONE_BYTE_STRING_TYPE]["parent"] =
        offsetof(SlicedString, parent_);
  }
  return fields;
}

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8
