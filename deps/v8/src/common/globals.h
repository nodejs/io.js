// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_GLOBALS_H_
#define V8_COMMON_GLOBALS_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <ostream>

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/base/build_config.h"
#include "src/base/enum-set.h"
#include "src/base/flags.h"
#include "src/base/logging.h"
#include "src/base/macros.h"

#define V8_INFINITY std::numeric_limits<double>::infinity()

// AIX has jmpbuf redefined as __jmpbuf in /usr/include/sys/context.h
// which replaces v8's jmpbuf , resulting in undefined symbol errors
#if defined(V8_OS_AIX) && defined(jmpbuf)
#undef jmpbuf
#endif

namespace v8 {

namespace base {
class Mutex;
class RecursiveMutex;
}  // namespace base

namespace internal {

// Determine whether we are running in a simulated environment.
// Setting USE_SIMULATOR explicitly from the build script will force
// the use of a simulated environment.
#if !defined(USE_SIMULATOR)
#if (V8_TARGET_ARCH_ARM64 && !V8_HOST_ARCH_ARM64)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_ARM && !V8_HOST_ARCH_ARM)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_PPC && !V8_HOST_ARCH_PPC)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_PPC64 && !V8_HOST_ARCH_PPC64)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_MIPS64 && !V8_HOST_ARCH_MIPS64)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_S390 && !V8_HOST_ARCH_S390)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_RISCV64 && !V8_HOST_ARCH_RISCV64)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_RISCV32 && !V8_HOST_ARCH_RISCV32)
#define USE_SIMULATOR 1
#endif
#if (V8_TARGET_ARCH_LOONG64 && !V8_HOST_ARCH_LOONG64)
#define USE_SIMULATOR 1
#endif
#endif

#if USE_SIMULATOR
#define USE_SIMULATOR_BOOL true
#else
#define USE_SIMULATOR_BOOL false
#endif

// Determine whether the architecture uses an embedded constant pool
// (contiguous constant pool embedded in code object).
#if V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#define V8_EMBEDDED_CONSTANT_POOL_BOOL true
#else
#define V8_EMBEDDED_CONSTANT_POOL_BOOL false
#endif

#ifdef DEBUG
#define DEBUG_BOOL true
#else
#define DEBUG_BOOL false
#endif

#ifdef V8_MAP_PACKING
#define V8_MAP_PACKING_BOOL true
#else
#define V8_MAP_PACKING_BOOL false
#endif

#ifdef V8_COMPRESS_POINTERS
#define COMPRESS_POINTERS_BOOL true
#else
#define COMPRESS_POINTERS_BOOL false
#endif

#if COMPRESS_POINTERS_BOOL && V8_TARGET_ARCH_X64
#define DECOMPRESS_POINTER_BY_ADDRESSING_MODE true
#else
#define DECOMPRESS_POINTER_BY_ADDRESSING_MODE false
#endif

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
#define COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL true
#else
#define COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL false
#endif

#if COMPRESS_POINTERS_BOOL && !COMPRESS_POINTERS_IN_SHARED_CAGE_BOOL
#define COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL true
#else
#define COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL false
#endif

#if defined(V8_SHARED_RO_HEAP) &&                     \
    (!defined(V8_COMPRESS_POINTERS) ||                \
     defined(V8_COMPRESS_POINTERS_IN_SHARED_CAGE)) && \
    !defined(V8_DISABLE_WRITE_BARRIERS)
#define V8_CAN_CREATE_SHARED_HEAP_BOOL true
#else
#define V8_CAN_CREATE_SHARED_HEAP_BOOL false
#endif

#ifdef V8_STATIC_ROOTS_GENERATION
#define V8_STATIC_ROOTS_GENERATION_BOOL true
#else
#define V8_STATIC_ROOTS_GENERATION_BOOL false
#endif

#ifdef V8_ENABLE_SANDBOX
#define V8_ENABLE_SANDBOX_BOOL true
#else
#define V8_ENABLE_SANDBOX_BOOL false
#endif

#if defined(V8_ENABLE_SANDBOX) && !defined(V8_DISABLE_LEAPTIERING)
// Initially, Leaptiering is only available on sandbox-enabled builds, and so
// V8_ENABLE_SANDBOX and V8_ENABLE_LEAPTIERING are effectively equivalent. Once
// completed there, it will be ported to non-sandbox builds, at which point the
// two defines will be separated from each other. Finally, once Leaptiering is
// used on all configurations, the define will be removed completely.
#define V8_ENABLE_LEAPTIERING 1
#define V8_ENABLE_LEAPTIERING_BOOL true
#else
#define V8_ENABLE_LEAPTIERING_BOOL false
#endif

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
#define ENABLE_CONTROL_FLOW_INTEGRITY_BOOL true
#else
#define ENABLE_CONTROL_FLOW_INTEGRITY_BOOL false
#endif

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64
// Set stack limit lower for ARM and ARM64 than for other architectures because:
//  - on Arm stack allocating MacroAssembler takes 120K bytes.
//    See issue crbug.com/405338
//  - on Arm64 when running in single-process mode for Android WebView, when
//    initializing V8 we already have a large stack and so have to set the
//    limit lower. See issue crbug.com/v8/10575
#define V8_DEFAULT_STACK_SIZE_KB 864
#elif V8_TARGET_ARCH_IA32
// In mid-2022, we're observing an increase in stack overflow crashes on
// 32-bit Windows; the suspicion is that some third-party software suddenly
// started to consume a lot more stack memory (before V8 is even initialized).
// So we speculatively lower the ia32 limit to the ARM limit for the time
// being. See crbug.com/1346791.
#define V8_DEFAULT_STACK_SIZE_KB 864
#elif V8_USE_ADDRESS_SANITIZER
// ASan makes C++ frames consume more stack, so V8 should leave more stack
// space available in case a C++ call happens. ClusterFuzz found a case where
// even just 1 KB less than the default stack size would be enough (see
// crbug.com/1486275); to be more robust towards future CF reports we'll
// use an even lower limit.
#define V8_DEFAULT_STACK_SIZE_KB 960
#else
// Slightly less than 1MB, since Windows' default stack size for
// the main execution thread is 1MB.
#define V8_DEFAULT_STACK_SIZE_KB 984
#endif

// Helper macros to enable handling of direct C calls in the simulator.
#if defined(USE_SIMULATOR) &&                                           \
    (defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_MIPS64) || \
     defined(V8_TARGET_ARCH_LOONG64) || defined(V8_TARGET_ARCH_RISCV64))
#define V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
#define V8_IF_USE_SIMULATOR(V) , V
#else
#define V8_IF_USE_SIMULATOR(V)
#endif  // defined(USE_SIMULATOR) && \
        // (defined(V8_TARGET_ARCH_ARM64) || defined(V8_TARGET_ARCH_MIPS64) || \
        // defined(V8_TARGET_ARCH_LOONG64))

// Minimum stack size in KB required by compilers.
constexpr int kStackSpaceRequiredForCompilation = 40;

// In order to emit more efficient stack checks in optimized code,
// deoptimization may implicitly exceed the V8 stack limit by this many bytes.
// Stack checks in functions with `difference between optimized and unoptimized
// stack frame sizes <= slack` can simply emit the simple stack check.
constexpr int kStackLimitSlackForDeoptimizationInBytes = 256;

// Sanity-check, assuming that we aim for a real OS stack size of at least 1MB.
static_assert(V8_DEFAULT_STACK_SIZE_KB * KB +
                  kStackLimitSlackForDeoptimizationInBytes <=
              MB);

// The V8_ENABLE_NEAR_CODE_RANGE_BOOL enables logic that tries to allocate
// code range within a pc-relative call/jump proximity from embedded builtins.
// This machinery could help only when we have an opportunity to choose where
// to allocate code range and could benefit from it. This is the case for the
// following configurations:
// - external code space AND pointer compression are enabled,
// - short builtin calls feature is enabled while pointer compression is not.
#if (defined(V8_SHORT_BUILTIN_CALLS) && !defined(V8_COMPRESS_POINTERS)) || \
    defined(V8_EXTERNAL_CODE_SPACE)
#define V8_ENABLE_NEAR_CODE_RANGE_BOOL true
#else
#define V8_ENABLE_NEAR_CODE_RANGE_BOOL false
#endif

// This constant is used for detecting whether the machine has >= 4GB of
// physical memory by checking the max old space size.
const size_t kShortBuiltinCallsOldSpaceSizeThreshold = size_t{2} * GB;

// Determine whether dict mode prototypes feature is enabled.
#ifdef V8_ENABLE_SWISS_NAME_DICTIONARY
#define V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL true
#else
#define V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL false
#endif

// Determine whether dict property constness tracking feature is enabled.
#ifdef V8_DICT_PROPERTY_CONST_TRACKING
#define V8_DICT_PROPERTY_CONST_TRACKING_BOOL true
#else
#define V8_DICT_PROPERTY_CONST_TRACKING_BOOL false
#endif

#ifdef V8_EXTERNAL_CODE_SPACE
#define V8_EXTERNAL_CODE_SPACE_BOOL true
#else
#define V8_EXTERNAL_CODE_SPACE_BOOL false
#endif

// V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT controls how V8 sets permissions for
// executable pages.
// In particular,
// 1) when memory region is reserved for code range, the whole region is
//    committed with RWX permissions and then the whole region is discarded,
// 2) since reconfiguration of RWX page permissions is not allowed on MacOS on
//    ARM64 ("Apple M1"/Apple Silicon), there must be no attempts to change
//    them,
// 3) the request to set RWX permissions in the execeutable page region just
//    commits the pages without changing permissions (see (1), they were already
//    allocated as RWX and then deommitted),
// 4) in order to make executable pages inaccessible one must use
//    OS::DiscardSystemPages() instead of using OS::DecommitPages() or setting
//    permissions to kNoAccess because the latter two are not allowed by the
//    MacOS (see (2)).
//
// This is applicable only to MacOS on ARM64 ("Apple M1"/Apple Silicon) which
// has a APRR/MAP_JIT machinery for fast W^X permission switching (see
// pthread_jit_write_protect).
//
// This approach doesn't work and shouldn't be used for V8 configuration with
// enabled pointer compression and disabled external code space because
// a) the pointer compression cage has to be reserved with MAP_JIT flag which
//    is too expensive,
// b) in case of shared pointer compression cage if the code range will be
//    deleted while the cage is still alive then attempt to configure
//    permissions of pages that were previously set to RWX will fail.
//
#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT && \
    !(defined(V8_COMPRESS_POINTERS) && !defined(V8_EXTERNAL_CODE_SPACE))
#define V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT true
#else
#define V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT false
#endif

// Protect the JavaScript heap with BrowserEngineKit APIs.
#if V8_HAS_BECORE_JIT_WRITE_PROTECT && \
    !(defined(V8_COMPRESS_POINTERS) && !defined(V8_EXTERNAL_CODE_SPACE))
#define V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT true
#else
#define V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT false
#endif

// Protect the JavaScript heap with memory protection keys.
#if V8_HAS_PKU_JIT_WRITE_PROTECT && \
    !(defined(V8_COMPRESS_POINTERS) && !defined(V8_EXTERNAL_CODE_SPACE))
#define V8_HEAP_USE_PKU_JIT_WRITE_PROTECT true
#else
#define V8_HEAP_USE_PKU_JIT_WRITE_PROTECT false
#endif

// Enable hardware features to make the sandbox memory temporarily inaccessible.
// This is currently only used with pkeys and in debug mode.
// TODO(sroettger): add a gn arg to toggle this once we enable it in non-debug
//                  builds.
#if V8_HAS_PKU_JIT_WRITE_PROTECT && defined(V8_ENABLE_SANDBOX) && defined(DEBUG)
#define V8_ENABLE_SANDBOX_HARDWARE_SUPPORT true
#else
#define V8_ENABLE_SANDBOX_HARDWARE_SUPPORT false
#endif

// Determine whether tagged pointers are 8 bytes (used in Torque layouts for
// choosing where to insert padding).
#if V8_TARGET_ARCH_64_BIT && !defined(V8_COMPRESS_POINTERS)
#define TAGGED_SIZE_8_BYTES true
#else
#define TAGGED_SIZE_8_BYTES false
#endif

#if defined(V8_OS_WIN) && defined(V8_TARGET_ARCH_X64)
#define V8_OS_WIN_X64 true
#endif

#if defined(V8_OS_WIN) && defined(V8_TARGET_ARCH_ARM64)
#define V8_OS_WIN_ARM64 true
#endif

#if defined(V8_OS_WIN_X64) || defined(V8_OS_WIN_ARM64)
#define V8_OS_WIN64 true
#endif

// Support for floating point parameters in calls to C.
// It's currently enabled only for the platforms listed below. We don't plan
// to add support for IA32, because it has a totally different approach
// (using FP stack).
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64) ||      \
    defined(V8_TARGET_ARCH_MIPS64) || defined(V8_TARGET_ARCH_LOONG64) || \
    defined(V8_TARGET_ARCH_RISCV64)
#define V8_ENABLE_FP_PARAMS_IN_C_LINKAGE 1
#endif

// Superclass for classes only using static method functions.
// The subclass of AllStatic cannot be instantiated at all.
class AllStatic {
#ifdef DEBUG
 public:
  AllStatic() = delete;
#endif
};

// -----------------------------------------------------------------------------
// Constants

constexpr int kMaxInt = 0x7FFFFFFF;
constexpr int kMinInt = -kMaxInt - 1;
constexpr int kMaxInt8 = (1 << 7) - 1;
constexpr int kMinInt8 = -(1 << 7);
constexpr int kMaxUInt8 = (1 << 8) - 1;
constexpr int kMinUInt8 = 0;
constexpr int kMaxInt16 = (1 << 15) - 1;
constexpr int kMinInt16 = -(1 << 15);
constexpr int kMaxUInt16 = (1 << 16) - 1;
constexpr int kMinUInt16 = 0;
constexpr int kMaxInt31 = kMaxInt / 2;
constexpr int kMinInt31 = kMinInt / 2;

constexpr uint32_t kMaxUInt32 = 0xFFFFFFFFu;
constexpr int kMinUInt32 = 0;

constexpr int kInt8Size = sizeof(int8_t);
constexpr int kUInt8Size = sizeof(uint8_t);
constexpr int kByteSize = 1;
constexpr int kCharSize = sizeof(char);
constexpr int kShortSize = sizeof(short);  // NOLINT
constexpr int kInt16Size = sizeof(int16_t);
constexpr int kUInt16Size = sizeof(uint16_t);
constexpr int kIntSize = sizeof(int);
constexpr int kInt32Size = sizeof(int32_t);
constexpr int kInt64Size = sizeof(int64_t);
constexpr int kUInt32Size = sizeof(uint32_t);
constexpr int kSizetSize = sizeof(size_t);
constexpr int kFloat16Size = sizeof(uint16_t);
constexpr int kFloatSize = sizeof(float);
constexpr int kDoubleSize = sizeof(double);
constexpr int kIntptrSize = sizeof(intptr_t);
constexpr int kUIntptrSize = sizeof(uintptr_t);
constexpr int kSystemPointerSize = sizeof(void*);
constexpr int kSystemPointerHexDigits = kSystemPointerSize == 4 ? 8 : 12;
constexpr int kPCOnStackSize = kSystemPointerSize;
constexpr int kFPOnStackSize = kSystemPointerSize;

#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
constexpr int kElidedFrameSlots = kPCOnStackSize / kSystemPointerSize;
#else
constexpr int kElidedFrameSlots = 0;
#endif

constexpr int kDoubleSizeLog2 = 3;
// The maximal length of the string representation for a double value
// (e.g. "-2.2250738585072020E-308"). It is composed as follows:
// - 17 decimal digits, see base::kBase10MaximalLength (dtoa.h)
// - 1 sign
// - 1 decimal point
// - 1 E or e
// - 1 exponent sign
// - 3 exponent
constexpr int kMaxDoubleStringLength = 24;

// Total wasm code space per engine (i.e. per process) is limited to make
// certain attacks that rely on heap spraying harder.
// Do not access directly, but via the {--wasm-max-committed-code-mb} flag.
// Just below 4GB, such that {kMaxWasmCodeMemory} fits in a 32-bit size_t.
constexpr uint32_t kMaxCommittedWasmCodeMB = 4095;

// The actual maximum code space size used can be configured with
// --max-wasm-code-space-size. This constant is the default value, and at the
// same time the maximum allowed value (checked by the WasmCodeManager).
#if V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_LOONG64
// ARM64 and Loong64 only supports direct calls within a 128 MB range.
constexpr uint32_t kDefaultMaxWasmCodeSpaceSizeMb = 128;
#elif V8_TARGET_ARCH_PPC64
// Branches only take 26 bits.
constexpr uint32_t kDefaultMaxWasmCodeSpaceSizeMb = 32;
#else
// Use 1024 MB limit for code spaces on other platforms. This is smaller than
// the total allowed code space (kMaxWasmCodeMemory) to avoid unnecessarily
// big reservations, and to ensure that distances within a code space fit
// within a 32-bit signed integer.
constexpr uint32_t kDefaultMaxWasmCodeSpaceSizeMb = 1024;
#endif

// Align IsolateData to a most common CPU cache line size.
constexpr size_t kIsolateDataAlignment = 64;

#if V8_HOST_ARCH_64_BIT
constexpr int kSystemPointerSizeLog2 = 3;
constexpr intptr_t kIntptrSignBit =
    static_cast<intptr_t>(uintptr_t{0x8000000000000000});
constexpr bool kPlatformRequiresCodeRange = true;
#if (V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64) && \
    (V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64) && V8_OS_LINUX
constexpr size_t kMaximalCodeRangeSize = 512 * MB;
constexpr size_t kMinExpectedOSPageSize = 64 * KB;  // OS page on PPC Linux
#elif V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_LOONG64 || V8_TARGET_ARCH_RISCV64
constexpr size_t kMaximalCodeRangeSize =
    (COMPRESS_POINTERS_BOOL && !V8_EXTERNAL_CODE_SPACE_BOOL) ? 128 * MB
                                                             : 256 * MB;
constexpr size_t kMinExpectedOSPageSize = 4 * KB;  // OS page.
#elif V8_TARGET_ARCH_X64
constexpr size_t kMaximalCodeRangeSize =
    (COMPRESS_POINTERS_BOOL && !V8_EXTERNAL_CODE_SPACE_BOOL) ? 128 * MB
                                                             : 512 * MB;
constexpr size_t kMinExpectedOSPageSize = 4 * KB;  // OS page.
#else
constexpr size_t kMaximalCodeRangeSize = 128 * MB;
constexpr size_t kMinExpectedOSPageSize = 4 * KB;  // OS page.
#endif
#if V8_OS_WIN
constexpr size_t kMinimumCodeRangeSize = 4 * MB;
constexpr size_t kReservedCodeRangePages = 1;
#else
constexpr size_t kMinimumCodeRangeSize = 3 * MB;
constexpr size_t kReservedCodeRangePages = 0;
#endif

// These constants define the total trusted space memory per process.
constexpr size_t kMaximalTrustedRangeSize = 1 * GB;
constexpr size_t kMinimumTrustedRangeSize = 32 * MB;

#else  // V8_HOST_ARCH_64_BIT

constexpr int kSystemPointerSizeLog2 = 2;
constexpr intptr_t kIntptrSignBit = 0x80000000;
#if (V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64) && \
    (V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64) && V8_OS_LINUX
constexpr bool kPlatformRequiresCodeRange = false;
constexpr size_t kMaximalCodeRangeSize = 0 * MB;
constexpr size_t kMinimumCodeRangeSize = 0 * MB;
constexpr size_t kMinExpectedOSPageSize = 64 * KB;  // OS page on PPC Linux
#elif V8_TARGET_ARCH_RISCV32
constexpr bool kPlatformRequiresCodeRange = false;
constexpr size_t kMaximalCodeRangeSize = 2048LL * MB;
constexpr size_t kMinimumCodeRangeSize = 0 * MB;
constexpr size_t kMinExpectedOSPageSize = 4 * KB;  // OS page.
#else
constexpr bool kPlatformRequiresCodeRange = false;
constexpr size_t kMaximalCodeRangeSize = 0 * MB;
constexpr size_t kMinimumCodeRangeSize = 0 * MB;
constexpr size_t kMinExpectedOSPageSize = 4 * KB;  // OS page.
#endif
constexpr size_t kReservedCodeRangePages = 0;
#endif  // V8_HOST_ARCH_64_BIT

static_assert(kSystemPointerSize == (1 << kSystemPointerSizeLog2));

#ifdef V8_COMPRESS_ZONES
#define COMPRESS_ZONES_BOOL true
#else
#define COMPRESS_ZONES_BOOL false
#endif  // V8_COMPRESS_ZONES

// The flag controls whether zones pointer compression should be enabled for
// TurboFan graphs or not.
static constexpr bool kCompressGraphZone = COMPRESS_ZONES_BOOL;

#ifdef V8_COMPRESS_POINTERS
static_assert(
    kSystemPointerSize == kInt64Size,
    "Pointer compression can be enabled only for 64-bit architectures");

constexpr int kTaggedSize = kInt32Size;
constexpr int kTaggedSizeLog2 = 2;

// These types define raw and atomic storage types for tagged values stored
// on V8 heap.
using Tagged_t = uint32_t;
using AtomicTagged_t = base::Atomic32;

#else

constexpr int kTaggedSize = kSystemPointerSize;
constexpr int kTaggedSizeLog2 = kSystemPointerSizeLog2;

// These types define raw and atomic storage types for tagged values stored
// on V8 heap.
using Tagged_t = Address;
using AtomicTagged_t = base::AtomicWord;

#endif  // V8_COMPRESS_POINTERS

static_assert(kTaggedSize == (1 << kTaggedSizeLog2));
static_assert((kTaggedSize == 8) == TAGGED_SIZE_8_BYTES);

using AsAtomicTagged = base::AsAtomicPointerImpl<AtomicTagged_t>;
static_assert(sizeof(Tagged_t) == kTaggedSize);
static_assert(sizeof(AtomicTagged_t) == kTaggedSize);

static_assert(kTaggedSize == kApiTaggedSize);

// TODO(ishell): use kTaggedSize or kSystemPointerSize instead.
#ifndef V8_COMPRESS_POINTERS
constexpr int kPointerSize = kSystemPointerSize;
constexpr int kPointerSizeLog2 = kSystemPointerSizeLog2;
static_assert(kPointerSize == (1 << kPointerSizeLog2));
#endif

#ifdef V8_COMPRESS_POINTERS_8GB
// To support 8GB heaps, all alocations are aligned to at least 8 bytes.
#define V8_COMPRESS_POINTERS_8GB_BOOL true
#else
#define V8_COMPRESS_POINTERS_8GB_BOOL false
#endif

// This type defines the raw storage type for external (or off-V8 heap) pointers
// stored on V8 heap.
constexpr int kExternalPointerSlotSize = sizeof(ExternalPointer_t);
#ifdef V8_ENABLE_SANDBOX
static_assert(kExternalPointerSlotSize == kTaggedSize);
#else
static_assert(kExternalPointerSlotSize == kSystemPointerSize);
#endif

// The storage type for pointers referring to CppHeap objects stored on the V8
// heap.
constexpr int kCppHeapPointerSlotSize = sizeof(CppHeapPointer_t);
#ifdef V8_COMPRESS_POINTERS
static_assert(kCppHeapPointerSlotSize == sizeof(uint32_t));
#else
static_assert(kCppHeapPointerSlotSize == kSystemPointerSize);
#endif

constexpr int kIndirectPointerSize = sizeof(IndirectPointerHandle);
// When the sandbox is enabled, trusted pointers are implemented as indirect
// pointers (indices into the trusted pointer table). Otherwise they are regular
// tagged pointers.
#ifdef V8_ENABLE_SANDBOX
constexpr int kTrustedPointerSize = kIndirectPointerSize;
using TrustedPointer_t = TrustedPointerHandle;
#else
constexpr int kTrustedPointerSize = kTaggedSize;
using TrustedPointer_t = Tagged_t;
#endif
constexpr int kCodePointerSize = kTrustedPointerSize;

// Pointers between trusted objects use compressed pointers with the trusted
// space base when the sandbox is enabled. Otherwise, they are regular tagged
// pointers. Either way, they are always kTaggedSize fields.
constexpr int kProtectedPointerSize = kTaggedSize;

#ifdef V8_ENABLE_LEAPTIERING
constexpr int kJSDispatchHandleSize = sizeof(JSDispatchHandle);
#endif

constexpr int kEmbedderDataSlotSize = kSystemPointerSize;

constexpr int kEmbedderDataSlotSizeInTaggedSlots =
    kEmbedderDataSlotSize / kTaggedSize;
static_assert(kEmbedderDataSlotSize >= kSystemPointerSize);

constexpr int kExternalAllocationSoftLimit =
    internal::Internals::kExternalAllocationSoftLimit;

// Maximum object size that gets allocated into regular pages. Objects larger
// than that size are allocated in large object space and are never moved in
// memory. This also applies to new space allocation, since objects are never
// migrated from new space to large object space. Takes double alignment into
// account.
//
// Current value: half of the page size.
constexpr int kMaxRegularHeapObjectSize = (1 << (kPageSizeBits - 1));

constexpr int kBitsPerByte = 8;
constexpr int kBitsPerByteLog2 = 3;
constexpr int kBitsPerSystemPointer = kSystemPointerSize * kBitsPerByte;
constexpr int kBitsPerSystemPointerLog2 =
    kSystemPointerSizeLog2 + kBitsPerByteLog2;
constexpr int kBitsPerInt = kIntSize * kBitsPerByte;

// IEEE 754 single precision floating point number bit layout.
constexpr uint32_t kBinary32SignMask = 0x80000000u;
constexpr uint32_t kBinary32ExponentMask = 0x7f800000u;
constexpr uint32_t kBinary32MantissaMask = 0x007fffffu;
constexpr int kBinary32ExponentBias = 127;
constexpr int kBinary32MaxExponent = 0xFE;
constexpr int kBinary32MinExponent = 0x01;
constexpr int kBinary32MantissaBits = 23;
constexpr int kBinary32ExponentShift = 23;

// Quiet NaNs have bits 51 to 62 set, possibly the sign bit, and no
// other bits set.
constexpr uint64_t kQuietNaNMask = static_cast<uint64_t>(0xfff) << 51;

constexpr int kOneByteSize = kCharSize;

// 128 bit SIMD value size.
constexpr int kSimd128Size = 16;

// 256 bit SIMD value size.
constexpr int kSimd256Size = 32;

// FUNCTION_ADDR(f) gets the address of a C function f.
#define FUNCTION_ADDR(f) (reinterpret_cast<v8::internal::Address>(f))

// FUNCTION_CAST<F>(addr) casts an address into a function
// of type F. Used to invoke generated code from within C.
template <typename F>
F FUNCTION_CAST(uint8_t* addr) {
  return reinterpret_cast<F>(reinterpret_cast<Address>(addr));
}

template <typename F>
F FUNCTION_CAST(Address addr) {
  return reinterpret_cast<F>(addr);
}

// Determine whether the architecture uses function descriptors
// which provide a level of indirection between the function pointer
// and the function entrypoint.
#if (V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64) &&                    \
    (V8_OS_AIX || (V8_TARGET_ARCH_PPC64 && V8_TARGET_BIG_ENDIAN && \
                   (!defined(_CALL_ELF) || _CALL_ELF == 1)))
#define USES_FUNCTION_DESCRIPTORS 1
#define FUNCTION_ENTRYPOINT_ADDRESS(f)       \
  (reinterpret_cast<v8::internal::Address*>( \
      &(reinterpret_cast<intptr_t*>(f)[0])))
#else
#define USES_FUNCTION_DESCRIPTORS 0
#endif

constexpr bool StaticStringsEqual(const char* s1, const char* s2) {
  for (;; ++s1, ++s2) {
    if (*s1 != *s2) return false;
    if (*s1 == '\0') return true;
  }
}

// -----------------------------------------------------------------------------
// Declarations for use in both the preparser and the rest of V8.

// The Strict Mode (ECMA-262 5th edition, 4.2.2).

enum class LanguageMode : bool { kSloppy, kStrict };
static const size_t LanguageModeSize = 2;

inline size_t hash_value(LanguageMode mode) {
  return static_cast<size_t>(mode);
}

inline const char* LanguageMode2String(LanguageMode mode) {
  switch (mode) {
    case LanguageMode::kSloppy:
      return "sloppy";
    case LanguageMode::kStrict:
      return "strict";
  }
  UNREACHABLE();
}

inline std::ostream& operator<<(std::ostream& os, LanguageMode mode) {
  return os << LanguageMode2String(mode);
}

inline bool is_sloppy(LanguageMode language_mode) {
  return language_mode == LanguageMode::kSloppy;
}

inline bool is_strict(LanguageMode language_mode) {
  return language_mode != LanguageMode::kSloppy;
}

inline bool is_valid_language_mode(int language_mode) {
  return language_mode == static_cast<int>(LanguageMode::kSloppy) ||
         language_mode == static_cast<int>(LanguageMode::kStrict);
}

inline LanguageMode construct_language_mode(bool strict_bit) {
  return static_cast<LanguageMode>(strict_bit);
}

// Return kStrict if either of the language modes is kStrict, or kSloppy
// otherwise.
inline LanguageMode stricter_language_mode(LanguageMode mode1,
                                           LanguageMode mode2) {
  static_assert(LanguageModeSize == 2);
  return static_cast<LanguageMode>(static_cast<int>(mode1) |
                                   static_cast<int>(mode2));
}

// A non-keyed store is of the form a.x = foo or a["x"] = foo whereas
// a keyed store is of the form a[expression] = foo.
enum class StoreOrigin { kMaybeKeyed, kNamed };

enum class TypeofMode { kInside, kNotInside };

// Whether floating point registers should be saved (and restored).
enum class SaveFPRegsMode { kIgnore, kSave };

// This enum describes the ownership semantics of an indirect pointer.
enum class IndirectPointerMode {
  // A regular reference from one HeapObject to another one through an indirect
  // pointer, where the referenced object should be kept alive as long as the
  // referencing object is alive.
  kStrong,
  // A reference from one HeapObject to another one through an indirect pointer
  // with custom ownership semantics. Used for example for references from
  // JSFunctions to Code objects which follow custom weak ownership semantics.
  kCustom
};

// Whether arguments are passed on a known stack location or through a
// register.
enum class ArgvMode { kStack, kRegister };

enum class CallApiCallbackMode {
  // This version of CallApiCallback used by IC system, it gets additional
  // target function argument which is used both for stack trace reconstruction
  // in case exception is thrown inside the callback and for callback
  // side-effects checking by debugger.
  kGeneric,

  // The following two versions are used for generating calls from optimized
  // code. They don't need to support side effects checking because function
  // will be deoptimized when side effects checking is enabled, and they don't
  // get the target function because it can be reconstructed from the lazy
  // deopt info in case exception is thrown.

  // This version is used for compiling code when Isolate profiling or runtime
  // call stats is disabled. The code that uses this version must be created
  // with a dependency on NoProfilingProtector.
  kOptimizedNoProfiling,

  // This version contains a dynamic check for enabled profiler and it supports
  // runtime call stats.
  kOptimized,
};

// This constant is used as an undefined value when passing source positions.
constexpr int kNoSourcePosition = -1;

// This constant is used to signal the function entry implicit stack check
// bytecode offset.
constexpr int kFunctionEntryBytecodeOffset = -1;

// This constant is used to signal the function exit interrupt budget handling
// bytecode offset.
constexpr int kFunctionExitBytecodeOffset = -1;

// This constant is used to indicate missing deoptimization information.
constexpr int kNoDeoptimizationId = -1;

// Deoptimize bailout kind:
// - Eager: a check failed in the optimized code and deoptimization happens
//   immediately.
// - Lazy: the code has been marked as dependent on some assumption which
//   is checked elsewhere and can trigger deoptimization the next time the
//   code is executed.
enum class DeoptimizeKind : uint8_t {
  kEager,
  kLazy,
};
constexpr DeoptimizeKind kFirstDeoptimizeKind = DeoptimizeKind::kEager;
constexpr DeoptimizeKind kLastDeoptimizeKind = DeoptimizeKind::kLazy;
static_assert(static_cast<int>(kFirstDeoptimizeKind) == 0);
constexpr int kDeoptimizeKindCount = static_cast<int>(kLastDeoptimizeKind) + 1;
inline size_t hash_value(DeoptimizeKind kind) {
  return static_cast<size_t>(kind);
}
constexpr const char* ToString(DeoptimizeKind kind) {
  switch (kind) {
    case DeoptimizeKind::kEager:
      return "Eager";
    case DeoptimizeKind::kLazy:
      return "Lazy";
  }
}
inline std::ostream& operator<<(std::ostream& os, DeoptimizeKind kind) {
  return os << ToString(kind);
}

// Indicates whether the lookup is related to sloppy-mode block-scoped
// function hoisting, and is a synthetic assignment for that.
enum class LookupHoistingMode { kNormal, kLegacySloppy };

inline std::ostream& operator<<(std::ostream& os,
                                const LookupHoistingMode& mode) {
  switch (mode) {
    case LookupHoistingMode::kNormal:
      return os << "normal hoisting";
    case LookupHoistingMode::kLegacySloppy:
      return os << "legacy sloppy hoisting";
  }
  UNREACHABLE();
}

static_assert(kSmiValueSize <= 32, "Unsupported Smi tagging scheme");
// Smi sign bit position must be 32-bit aligned so we can use sign extension
// instructions on 64-bit architectures without additional shifts.
static_assert((kSmiValueSize + kSmiShiftSize + kSmiTagSize) % 32 == 0,
              "Unsupported Smi tagging scheme");

constexpr bool kIsSmiValueInUpper32Bits =
    (kSmiValueSize + kSmiShiftSize + kSmiTagSize) == 64;
constexpr bool kIsSmiValueInLower32Bits =
    (kSmiValueSize + kSmiShiftSize + kSmiTagSize) == 32;
static_assert(!SmiValuesAre32Bits() == SmiValuesAre31Bits(),
              "Unsupported Smi tagging scheme");
static_assert(SmiValuesAre32Bits() == kIsSmiValueInUpper32Bits,
              "Unsupported Smi tagging scheme");
static_assert(SmiValuesAre31Bits() == kIsSmiValueInLower32Bits,
              "Unsupported Smi tagging scheme");

// Mask for the sign bit in a smi.
constexpr intptr_t kSmiSignMask = static_cast<intptr_t>(
    uintptr_t{1} << (kSmiValueSize + kSmiShiftSize + kSmiTagSize - 1));

// Desired alignment for tagged pointers.
constexpr int kObjectAlignmentBits = kTaggedSizeLog2;
constexpr intptr_t kObjectAlignment = 1 << kObjectAlignmentBits;
constexpr intptr_t kObjectAlignmentMask = kObjectAlignment - 1;

// Object alignment for 8GB pointer compressed heap.
constexpr intptr_t kObjectAlignment8GbHeap = 8;
constexpr intptr_t kObjectAlignment8GbHeapMask = kObjectAlignment8GbHeap - 1;

#ifdef V8_COMPRESS_POINTERS_8GB
static_assert(
    kObjectAlignment8GbHeap == 2 * kTaggedSize,
    "When the 8GB heap is enabled, all allocations should be aligned to twice "
    "the size of a tagged value.");
#endif

// Desired alignment for system pointers.
constexpr intptr_t kPointerAlignment = (1 << kSystemPointerSizeLog2);
constexpr intptr_t kPointerAlignmentMask = kPointerAlignment - 1;

// Desired alignment for double values.
constexpr intptr_t kDoubleAlignment = 8;
constexpr intptr_t kDoubleAlignmentMask = kDoubleAlignment - 1;

// Desired alignment for generated code is 64 bytes on x64 (to allow 64-bytes
// loop header alignment) and 32 bytes (to improve cache line utilization) on
// other architectures.
#if V8_TARGET_ARCH_X64
constexpr int kCodeAlignmentBits = 6;
#elif V8_TARGET_ARCH_PPC64
// 64 byte alignment is needed on ppc64 to make sure p10 prefixed instructions
// don't cross 64-byte boundaries.
constexpr int kCodeAlignmentBits = 6;
#else
constexpr int kCodeAlignmentBits = 5;
#endif
constexpr intptr_t kCodeAlignment = 1 << kCodeAlignmentBits;
constexpr intptr_t kCodeAlignmentMask = kCodeAlignment - 1;

const Address kWeakHeapObjectMask = 1 << 1;

// The lower 32 bits of the cleared weak reference value is always equal to
// the |kClearedWeakHeapObjectLower32| constant but on 64-bit architectures
// the value of the upper 32 bits part may be
// 1) zero when pointer compression is disabled,
// 2) upper 32 bits of the isolate root value when pointer compression is
//    enabled.
// This is necessary to make pointer decompression computation also suitable
// for cleared weak reference.
// Note, that real heap objects can't have lower 32 bits equal to 3 because
// this offset belongs to page header. So, in either case it's enough to
// compare only the lower 32 bits of a Tagged<MaybeObject> value in order to
// figure out if it's a cleared reference or not.
const uint32_t kClearedWeakHeapObjectLower32 = 3;

// Zap-value: The value used for zapping dead objects.
// Should be a recognizable hex value tagged as a failure.
#ifdef V8_HOST_ARCH_64_BIT
constexpr uint64_t kClearedFreeMemoryValue = 0;
constexpr uint64_t kZapValue = uint64_t{0xdeadbeedbeadbeef};
constexpr uint64_t kHandleZapValue = uint64_t{0x1baddead0baddeaf};
constexpr uint64_t kGlobalHandleZapValue = uint64_t{0x1baffed00baffedf};
constexpr uint64_t kTracedHandleEagerResetZapValue =
    uint64_t{0x1beffedaabaffedf};
constexpr uint64_t kTracedHandleMinorGCResetZapValue =
    uint64_t{0x1beffedeebaffedf};
constexpr uint64_t kTracedHandleMinorGCWeakResetZapValue =
    uint64_t{0x1beffed11baffedf};
constexpr uint64_t kTracedHandleFullGCResetZapValue =
    uint64_t{0x1beffed77baffedf};
constexpr uint64_t kFromSpaceZapValue = uint64_t{0x1beefdad0beefdaf};
constexpr uint64_t kDebugZapValue = uint64_t{0xbadbaddbbadbaddb};
constexpr uint64_t kSlotsZapValue = uint64_t{0xbeefdeadbeefdeef};
constexpr uint64_t kFreeListZapValue = 0xfeed1eaffeed1eaf;
#else
constexpr uint32_t kClearedFreeMemoryValue = 0;
constexpr uint32_t kZapValue = 0xdeadbeef;
constexpr uint32_t kHandleZapValue = 0xbaddeaf;
constexpr uint32_t kGlobalHandleZapValue = 0xbaffedf;
constexpr uint32_t kTracedHandleEagerResetZapValue = 0xbeffedf;
constexpr uint32_t kTracedHandleMinorGCResetZapValue = 0xbeffadf;
constexpr uint32_t kTracedHandleMinorGCWeakResetZapValue = 0xbe11adf;
constexpr uint32_t kTracedHandleFullGCResetZapValue = 0xbe77adf;
constexpr uint32_t kFromSpaceZapValue = 0xbeefdaf;
constexpr uint32_t kSlotsZapValue = 0xbeefdeef;
constexpr uint32_t kDebugZapValue = 0xbadbaddb;
constexpr uint32_t kFreeListZapValue = 0xfeed1eaf;
#endif

constexpr int kCodeZapValue = 0xbadc0de;
constexpr uint32_t kPhantomReferenceZap = 0xca11bac;

// On Intel architecture, cache line size is 64 bytes.
// On ARM it may be less (32 bytes), but as far this constant is
// used for aligning data, it doesn't hurt to align on a greater value.
#define PROCESSOR_CACHE_LINE_SIZE 64

// Constants relevant to double precision floating point numbers.
// If looking only at the top 32 bits, the QNaN mask is bits 19 to 30.
constexpr uint32_t kQuietNaNHighBitsMask = 0xfff << (51 - 32);

enum class V8_EXPORT_ENUM HeapObjectReferenceType {
  WEAK,
  STRONG,
};

enum class ArgumentsType {
  kRuntime,
  kJS,
};

// -----------------------------------------------------------------------------
// Forward declarations for frequently used classes

class AccessorInfo;
template <ArgumentsType>
class Arguments;
using RuntimeArguments = Arguments<ArgumentsType::kRuntime>;
using JavaScriptArguments = Arguments<ArgumentsType::kJS>;
class Assembler;
class ClassScope;
class InstructionStream;
class BigInt;
class Code;
class CodeSpace;
class Context;
class DeclarationScope;
class Debug;
class DebugInfo;
class Descriptor;
class DescriptorArray;
#ifdef V8_ENABLE_DIRECT_HANDLE
template <typename T>
class DirectHandle;
template <typename T>
class DirectHandleVector;
#endif
class TransitionArray;
class ExternalReference;
class ExposedTrustedObject;
class FeedbackVector;
class FixedArray;
class Foreign;
class FreeStoreAllocationPolicy;
class FunctionTemplateInfo;
class GlobalDictionary;
template <typename T>
class Handle;
#ifndef V8_ENABLE_DIRECT_HANDLE
template <typename T>
using DirectHandle = Handle<T>;
#endif
class Heap;
class HeapNumber;
class Boolean;
class Null;
class Undefined;
class HeapObject;
class IC;
template <typename T>
using IndirectHandle = Handle<T>;
class InterceptorInfo;
class Isolate;
class JSReceiver;
class JSArray;
class JSFunction;
class JSObject;
class JSProxy;
class JSBoundFunction;
class JSWrappedFunction;
class LocalIsolate;
class MacroAssembler;
class Map;
class MarkCompactCollector;
#ifdef V8_ENABLE_DIRECT_HANDLE
template <typename T>
class MaybeDirectHandle;
#endif
template <typename T>
class MaybeHandle;
#ifndef V8_ENABLE_DIRECT_HANDLE
template <typename T>
using MaybeDirectHandle = MaybeHandle<T>;
#endif
template <typename T>
using MaybeIndirectHandle = MaybeHandle<T>;
#ifdef V8_ENABLE_DIRECT_HANDLE
class MaybeObjectDirectHandle;
#endif
class MaybeObjectHandle;
#ifndef V8_ENABLE_DIRECT_HANDLE
using MaybeObjectDirectHandle = MaybeObjectHandle;
#endif
using MaybeObjectIndirectHandle = MaybeObjectHandle;
template <typename T>
class MaybeWeak;
class MutablePageMetadata;
class MessageLocation;
class ModuleScope;
class Name;
class NameDictionary;
class NativeContext;
class NewSpace;
class NewLargeObjectSpace;
class NumberDictionary;
class Object;
class OldLargeObjectSpace;
template <HeapObjectReferenceType kRefType, typename StorageType>
class TaggedImpl;
class StrongTaggedValue;
class TaggedValue;
class CompressedObjectSlot;
class CompressedMaybeObjectSlot;
class CompressedMapWordSlot;
class CompressedHeapObjectSlot;
template <typename Cage>
class V8HeapCompressionSchemeImpl;
class MainCage;
using V8HeapCompressionScheme = V8HeapCompressionSchemeImpl<MainCage>;
#ifdef V8_ENABLE_SANDBOX
class TrustedCage;
using TrustedSpaceCompressionScheme = V8HeapCompressionSchemeImpl<TrustedCage>;
#else
// The trusted cage does not exist in this case.
using TrustedSpaceCompressionScheme = V8HeapCompressionScheme;
#endif
class ExternalCodeCompressionScheme;
template <typename CompressionScheme>
class OffHeapCompressedObjectSlot;
class FullObjectSlot;
class FullMaybeObjectSlot;
class FullHeapObjectSlot;
class OffHeapFullObjectSlot;
class OldSpace;
class ReadOnlySpace;
class RelocInfo;
class Scope;
class ScopeInfo;
class Script;
class SimpleNumberDictionary;
class Smi;
template <typename Config, class Allocator = FreeStoreAllocationPolicy>
class SplayTree;
class String;
class StringStream;
class Struct;
class Symbol;
template <typename T>
class Tagged;
template <typename... Ts>
class Union;
class Variable;
namespace maglev {
class MaglevAssembler;
}
namespace compiler {
class AccessBuilder;
}

// Number is either a Smi or a HeapNumber.
using Number = Union<Smi, HeapNumber>;
// Numeric is either a Number or a BigInt.
using Numeric = Union<Smi, HeapNumber, BigInt>;
// A primitive JavaScript value, which excludes JS objects.
using JSPrimitive =
    Union<Smi, HeapNumber, BigInt, String, Symbol, Boolean, Null, Undefined>;
// A user-exposed JavaScript value, as opposed to V8-internal values like Holes
// or a FixedArray.
using JSAny = Union<Smi, HeapNumber, BigInt, String, Symbol, Boolean, Null,
                    Undefined, JSReceiver>;
using JSAnyNotNumeric =
    Union<String, Symbol, Boolean, Null, Undefined, JSReceiver>;
using JSAnyNotNumber =
    Union<BigInt, String, Symbol, Boolean, Null, Undefined, JSReceiver>;
using JSCallable =
    Union<JSBoundFunction, JSFunction, JSObject, JSProxy, JSWrappedFunction>;
using MaybeObject = MaybeWeak<Object>;
using HeapObjectReference = MaybeWeak<HeapObject>;

using JSObjectOrUndefined = Union<JSObject, Undefined>;

// Slots are either full-pointer slots or compressed slots depending on whether
// pointer compression is enabled or not.
struct SlotTraits {
#ifdef V8_COMPRESS_POINTERS
  using TObjectSlot = CompressedObjectSlot;
  using TMaybeObjectSlot = CompressedMaybeObjectSlot;
  using THeapObjectSlot = CompressedHeapObjectSlot;
  using TOffHeapObjectSlot =
      OffHeapCompressedObjectSlot<V8HeapCompressionScheme>;
#ifdef V8_EXTERNAL_CODE_SPACE
  using TInstructionStreamSlot =
      OffHeapCompressedObjectSlot<ExternalCodeCompressionScheme>;
#else
  using TInstructionStreamSlot = TObjectSlot;
#endif  // V8_EXTERNAL_CODE_SPACE
#else
  using TObjectSlot = FullObjectSlot;
  using TMaybeObjectSlot = FullMaybeObjectSlot;
  using THeapObjectSlot = FullHeapObjectSlot;
  using TOffHeapObjectSlot = OffHeapFullObjectSlot;
  using TInstructionStreamSlot = OffHeapFullObjectSlot;
#endif  // V8_COMPRESS_POINTERS
#ifdef V8_ENABLE_SANDBOX
  using TProtectedPointerSlot =
      OffHeapCompressedObjectSlot<TrustedSpaceCompressionScheme>;
#else
  using TProtectedPointerSlot = TObjectSlot;
#endif  // V8_ENABLE_SANDBOX
};

// An ObjectSlot instance describes a kTaggedSize-sized on-heap field ("slot")
// holding an Object value (smi or strong heap object).
using ObjectSlot = SlotTraits::TObjectSlot;

// A MaybeObjectSlot instance describes a kTaggedSize-sized on-heap field
// ("slot") holding Tagged<MaybeObject> (smi or weak heap object or strong heap
// object).
using MaybeObjectSlot = SlotTraits::TMaybeObjectSlot;

// A HeapObjectSlot instance describes a kTaggedSize-sized field ("slot")
// holding a weak or strong pointer to a heap object (think:
// Tagged<HeapObjectReference>).
using HeapObjectSlot = SlotTraits::THeapObjectSlot;

// An OffHeapObjectSlot instance describes a kTaggedSize-sized field ("slot")
// holding an Object value (smi or strong heap object), whose slot location is
// off-heap.
using OffHeapObjectSlot = SlotTraits::TOffHeapObjectSlot;

// A InstructionStreamSlot instance describes a kTaggedSize-sized field
// ("slot") holding a strong pointer to a InstructionStream object. The
// InstructionStream object slots might be compressed and since code space might
// be allocated off the main heap the load operations require explicit cage base
// value for code space.
using InstructionStreamSlot = SlotTraits::TInstructionStreamSlot;

// A protected pointer is one where both the pointer itself and the pointed-to
// object are protected from modifications by an attacker if the sandbox is
// enabled. In practice, this means that they are pointers from one
// TrustedObject to another TrustedObject as (only) trusted objects cannot
// directly be manipulated by an attacker.
using ProtectedPointerSlot = SlotTraits::TProtectedPointerSlot;

using WeakSlotCallback = bool (*)(FullObjectSlot pointer);

using WeakSlotCallbackWithHeap = bool (*)(Heap* heap, FullObjectSlot pointer);

// -----------------------------------------------------------------------------
// Miscellaneous

// NOTE: SpaceIterator depends on AllocationSpace enumeration values being
// consecutive.
enum AllocationSpace {
  RO_SPACE,       // Immortal, immovable and immutable objects,
  NEW_SPACE,      // Young generation space for regular objects collected
                  // with Scavenger/MinorMS.
  OLD_SPACE,      // Old generation regular object space.
  CODE_SPACE,     // Old generation code object space, marked executable.
  SHARED_SPACE,   // Space shared between multiple isolates. Optional.
  TRUSTED_SPACE,  // Space for trusted objects. When the sandbox is enabled,
                  // this space will be located outside of it so that objects in
                  // it cannot directly be corrupted by an attacker.
  SHARED_TRUSTED_SPACE,     // Trusted space but for shared objects. Optional.
  NEW_LO_SPACE,             // Young generation large object space.
  LO_SPACE,                 // Old generation large object space.
  CODE_LO_SPACE,            // Old generation large code object space.
  SHARED_LO_SPACE,          // Space shared between multiple isolates. Optional.
  SHARED_TRUSTED_LO_SPACE,  // Like TRUSTED_SPACE but for shared large objects.
  TRUSTED_LO_SPACE,         // Like TRUSTED_SPACE but for large objects.

  FIRST_SPACE = RO_SPACE,
  LAST_SPACE = TRUSTED_LO_SPACE,
  FIRST_MUTABLE_SPACE = NEW_SPACE,
  LAST_MUTABLE_SPACE = TRUSTED_LO_SPACE,
  FIRST_GROWABLE_PAGED_SPACE = OLD_SPACE,
  LAST_GROWABLE_PAGED_SPACE = TRUSTED_SPACE,
  FIRST_SWEEPABLE_SPACE = NEW_SPACE,
  LAST_SWEEPABLE_SPACE = SHARED_TRUSTED_SPACE
};
constexpr int kSpaceTagSize = 4;
static_assert(FIRST_SPACE == 0);

constexpr bool IsAnyCodeSpace(AllocationSpace space) {
  return space == CODE_SPACE || space == CODE_LO_SPACE;
}
constexpr bool IsAnyTrustedSpace(AllocationSpace space) {
  return space == TRUSTED_SPACE || space == TRUSTED_LO_SPACE ||
         space == SHARED_TRUSTED_SPACE || space == SHARED_TRUSTED_LO_SPACE;
}
constexpr bool IsAnySharedSpace(AllocationSpace space) {
  return space == SHARED_SPACE || space == SHARED_LO_SPACE ||
         space == SHARED_TRUSTED_SPACE || space == SHARED_TRUSTED_LO_SPACE;
}
constexpr bool IsAnyNewSpace(AllocationSpace space) {
  return space == NEW_SPACE || space == NEW_LO_SPACE;
}

constexpr const char* ToString(AllocationSpace space) {
  switch (space) {
    case AllocationSpace::RO_SPACE:
      return "read_only_space";
    case AllocationSpace::NEW_SPACE:
      return "new_space";
    case AllocationSpace::OLD_SPACE:
      return "old_space";
    case AllocationSpace::CODE_SPACE:
      return "code_space";
    case AllocationSpace::SHARED_SPACE:
      return "shared_space";
    case AllocationSpace::TRUSTED_SPACE:
      return "trusted_space";
    case AllocationSpace::SHARED_TRUSTED_SPACE:
      return "shared_trusted_space";
    case AllocationSpace::NEW_LO_SPACE:
      return "new_large_object_space";
    case AllocationSpace::LO_SPACE:
      return "large_object_space";
    case AllocationSpace::CODE_LO_SPACE:
      return "code_large_object_space";
    case AllocationSpace::SHARED_LO_SPACE:
      return "shared_large_object_space";
    case AllocationSpace::SHARED_TRUSTED_LO_SPACE:
      return "shared_trusted_large_object_space";
    case AllocationSpace::TRUSTED_LO_SPACE:
      return "trusted_large_object_space";
  }
}

inline std::ostream& operator<<(std::ostream& os, AllocationSpace space) {
  return os << ToString(space);
}

enum class AllocationType : uint8_t {
  kYoung,  // Regular object allocated in NEW_SPACE or NEW_LO_SPACE.
  kOld,    // Regular object allocated in OLD_SPACE or LO_SPACE.
  kCode,   // InstructionStream object allocated in CODE_SPACE or CODE_LO_SPACE.
  kMap,    // Map object allocated in OLD_SPACE.
  kReadOnly,       // Object allocated in RO_SPACE.
  kSharedOld,      // Regular object allocated in OLD_SPACE in the shared heap.
  kSharedMap,      // Map object in OLD_SPACE in the shared heap.
  kSharedTrusted,  // Trusted objects in TRUSTED_SPACE in the shared heap.
  kTrusted,        // Object allocated in TRUSTED_SPACE or TRUSTED_LO_SPACE.
};

constexpr const char* ToString(AllocationType kind) {
  switch (kind) {
    case AllocationType::kYoung:
      return "Young";
    case AllocationType::kOld:
      return "Old";
    case AllocationType::kCode:
      return "Code";
    case AllocationType::kMap:
      return "Map";
    case AllocationType::kReadOnly:
      return "ReadOnly";
    case AllocationType::kSharedOld:
      return "SharedOld";
    case AllocationType::kSharedMap:
      return "SharedMap";
    case AllocationType::kTrusted:
      return "Trusted";
    case AllocationType::kSharedTrusted:
      return "SharedTrusted";
  }
}

inline std::ostream& operator<<(std::ostream& os, AllocationType type) {
  return os << ToString(type);
}

// Reason for a garbage collection.
//
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused. If you add new items here, update
// src/tools/metrics/histograms/enums.xml in chromium.
enum class GarbageCollectionReason : int {
  kUnknown = 0,
  kAllocationFailure = 1,
  kAllocationLimit = 2,
  kContextDisposal = 3,
  kCountersExtension = 4,
  kDebugger = 5,
  kDeserializer = 6,
  kExternalMemoryPressure = 7,
  kFinalizeMarkingViaStackGuard = 8,
  kFinalizeMarkingViaTask = 9,
  kFullHashtable = 10,
  kHeapProfiler = 11,
  kTask = 12,
  kLastResort = 13,
  kLowMemoryNotification = 14,
  kMakeHeapIterable = 15,
  kMemoryPressure = 16,
  kMemoryReducer = 17,
  kRuntime = 18,
  kSamplingProfiler = 19,
  kSnapshotCreator = 20,
  kTesting = 21,
  kExternalFinalize = 22,
  kGlobalAllocationLimit = 23,
  kMeasureMemory = 24,
  kBackgroundAllocationFailure = 25,
  kFinalizeConcurrentMinorMS = 26,
  kCppHeapAllocationFailure = 27,

  NUM_REASONS,
};

static_assert(kGarbageCollectionReasonMaxValue ==
                  static_cast<int>(GarbageCollectionReason::NUM_REASONS) - 1,
              "The value of kGarbageCollectionReasonMaxValue is inconsistent.");

constexpr const char* ToString(GarbageCollectionReason reason) {
  switch (reason) {
    case GarbageCollectionReason::kAllocationFailure:
      return "allocation failure";
    case GarbageCollectionReason::kAllocationLimit:
      return "allocation limit";
    case GarbageCollectionReason::kContextDisposal:
      return "context disposal";
    case GarbageCollectionReason::kCountersExtension:
      return "counters extension";
    case GarbageCollectionReason::kDebugger:
      return "debugger";
    case GarbageCollectionReason::kDeserializer:
      return "deserialize";
    case GarbageCollectionReason::kExternalMemoryPressure:
      return "external memory pressure";
    case GarbageCollectionReason::kFinalizeMarkingViaStackGuard:
      return "finalize incremental marking via stack guard";
    case GarbageCollectionReason::kFinalizeMarkingViaTask:
      return "finalize incremental marking via task";
    case GarbageCollectionReason::kFullHashtable:
      return "full hash-table";
    case GarbageCollectionReason::kHeapProfiler:
      return "heap profiler";
    case GarbageCollectionReason::kTask:
      return "task";
    case GarbageCollectionReason::kLastResort:
      return "last resort";
    case GarbageCollectionReason::kLowMemoryNotification:
      return "low memory notification";
    case GarbageCollectionReason::kMakeHeapIterable:
      return "make heap iterable";
    case GarbageCollectionReason::kMemoryPressure:
      return "memory pressure";
    case GarbageCollectionReason::kMemoryReducer:
      return "memory reducer";
    case GarbageCollectionReason::kRuntime:
      return "runtime";
    case GarbageCollectionReason::kSamplingProfiler:
      return "sampling profiler";
    case GarbageCollectionReason::kSnapshotCreator:
      return "snapshot creator";
    case GarbageCollectionReason::kTesting:
      return "testing";
    case GarbageCollectionReason::kExternalFinalize:
      return "external finalize";
    case GarbageCollectionReason::kGlobalAllocationLimit:
      return "global allocation limit";
    case GarbageCollectionReason::kMeasureMemory:
      return "measure memory";
    case GarbageCollectionReason::kUnknown:
      return "unknown";
    case GarbageCollectionReason::kBackgroundAllocationFailure:
      return "background allocation failure";
    case GarbageCollectionReason::kFinalizeConcurrentMinorMS:
      return "finalize concurrent MinorMS";
    case GarbageCollectionReason::kCppHeapAllocationFailure:
      return "CppHeap allocation failure";
    case GarbageCollectionReason::NUM_REASONS:
      UNREACHABLE();
  }
}

inline std::ostream& operator<<(std::ostream& os,
                                GarbageCollectionReason reason) {
  return os << ToString(reason);
}

inline size_t hash_value(AllocationType kind) {
  return static_cast<uint8_t>(kind);
}

inline constexpr bool IsSharedAllocationType(AllocationType kind) {
  return kind == AllocationType::kSharedOld ||
         kind == AllocationType::kSharedMap;
}

enum AllocationAlignment {
  // The allocated address is kTaggedSize aligned (this is default for most of
  // the allocations).
  kTaggedAligned,
  // The allocated address is kDoubleSize aligned.
  kDoubleAligned,
  // The (allocated address + kTaggedSize) is kDoubleSize aligned.
  kDoubleUnaligned
};

// TODO(ishell, v8:8875): Consider using aligned allocations once the
// allocation alignment inconsistency is fixed. For now we keep using
// tagged aligned (not double aligned) access since all our supported platforms
// allow tagged-aligned access to doubles and full words.
#define USE_ALLOCATION_ALIGNMENT_BOOL false

enum class AccessMode { ATOMIC, NON_ATOMIC };

enum MinimumCapacity {
  USE_DEFAULT_MINIMUM_CAPACITY,
  USE_CUSTOM_MINIMUM_CAPACITY
};

enum class GarbageCollector { SCAVENGER, MARK_COMPACTOR, MINOR_MARK_SWEEPER };

constexpr const char* ToString(GarbageCollector collector) {
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      return "Scavenger";
    case GarbageCollector::MARK_COMPACTOR:
      return "Mark-Sweep-Compact";
    case GarbageCollector::MINOR_MARK_SWEEPER:
      return "Minor Mark-Sweep";
  }
}

inline std::ostream& operator<<(std::ostream& os, GarbageCollector collector) {
  return os << ToString(collector);
}

enum class CompactionSpaceKind {
  kNone,
  kCompactionSpaceForScavenge,
  kCompactionSpaceForMarkCompact,
  kCompactionSpaceForMinorMarkSweep,
};

enum Executability { NOT_EXECUTABLE, EXECUTABLE };

enum class PageSize { kRegular, kLarge };

enum class CodeFlushMode {
  kFlushBytecode,
  kFlushBaselineCode,
  kStressFlushCode,
};

enum class ExternalBackingStoreType {
  kArrayBuffer,
  kExternalString,
  kNumValues
};

enum class NewJSObjectType : uint8_t {
  kNoAPIWrapper,
  kAPIWrapper,
};

bool inline IsBaselineCodeFlushingEnabled(base::EnumSet<CodeFlushMode> mode) {
  return mode.contains(CodeFlushMode::kFlushBaselineCode);
}

bool inline IsByteCodeFlushingEnabled(base::EnumSet<CodeFlushMode> mode) {
  return mode.contains(CodeFlushMode::kFlushBytecode);
}

bool inline IsStressFlushingEnabled(base::EnumSet<CodeFlushMode> mode) {
  return mode.contains(CodeFlushMode::kStressFlushCode);
}

bool inline IsFlushingDisabled(base::EnumSet<CodeFlushMode> mode) {
  return mode.empty();
}

// Indicates whether a script should be parsed and compiled in REPL mode.
enum class REPLMode {
  kYes,
  kNo,
};

inline REPLMode construct_repl_mode(bool is_repl_mode) {
  return is_repl_mode ? REPLMode::kYes : REPLMode::kNo;
}

// Indicates whether a script is parsed during debugging.
enum class ParsingWhileDebugging {
  kYes,
  kNo,
};

// Flag indicating whether code is built into the VM (one of the natives files).
enum NativesFlag { NOT_NATIVES_CODE, EXTENSION_CODE, INSPECTOR_CODE };

// ParseRestriction is used to restrict the set of valid statements in a
// unit of compilation.  Restriction violations cause a syntax error.
enum ParseRestriction : bool {
  NO_PARSE_RESTRICTION,         // All expressions are allowed.
  ONLY_SINGLE_FUNCTION_LITERAL  // Only a single FunctionLiteral expression.
};

enum class ScriptEventType {
  kReserveId,
  kCreate,
  kDeserialize,
  kBackgroundCompile,
  kStreamingCompileBackground,
  kStreamingCompileForeground
};

// State for inline cache call sites. Aliased as IC::State.
enum class InlineCacheState {
  // No feedback will be collected.
  NO_FEEDBACK,
  // Has never been executed.
  UNINITIALIZED,
  // Has been executed and only one receiver type has been seen.
  MONOMORPHIC,
  // Check failed due to prototype (or map deprecation).
  RECOMPUTE_HANDLER,
  // Multiple receiver types have been seen.
  POLYMORPHIC,
  // Many DOM receiver types have been seen for the same accessor.
  MEGADOM,
  // Many receiver types have been seen.
  MEGAMORPHIC,
  // A generic handler is installed and no extra typefeedback is recorded.
  GENERIC,
};

inline size_t hash_value(InlineCacheState mode) {
  return base::bit_cast<int>(mode);
}

// Printing support.
inline const char* InlineCacheState2String(InlineCacheState state) {
  switch (state) {
    case InlineCacheState::NO_FEEDBACK:
      return "NOFEEDBACK";
    case InlineCacheState::UNINITIALIZED:
      return "UNINITIALIZED";
    case InlineCacheState::MONOMORPHIC:
      return "MONOMORPHIC";
    case InlineCacheState::RECOMPUTE_HANDLER:
      return "RECOMPUTE_HANDLER";
    case InlineCacheState::POLYMORPHIC:
      return "POLYMORPHIC";
    case InlineCacheState::MEGAMORPHIC:
      return "MEGAMORPHIC";
    case InlineCacheState::MEGADOM:
      return "MEGADOM";
    case InlineCacheState::GENERIC:
      return "GENERIC";
  }
  UNREACHABLE();
}

enum WhereToStart { kStartAtReceiver, kStartAtPrototype };

enum ResultSentinel { kNotFound = -1, kUnsupported = -2 };

enum ShouldThrow {
  kDontThrow = Internals::kDontThrow,
  kThrowOnError = Internals::kThrowOnError,
};

// The result that might be returned by Setter/Definer/Deleter interceptor
// callback when it doesn't throw an exception.
enum class InterceptorResult {
  kFalse = 0,
  kTrue = 1,
  kNotIntercepted = 2,
};

enum class ThreadKind { kMain, kBackground };

// Union used for customized checking of the IEEE double types
// inlined within v8 runtime, rather than going to the underlying
// platform headers and libraries
union IeeeDoubleLittleEndianArchType {
  double d;
  struct {
    unsigned int man_low : 32;
    unsigned int man_high : 20;
    unsigned int exp : 11;
    unsigned int sign : 1;
  } bits;
};

union IeeeDoubleBigEndianArchType {
  double d;
  struct {
    unsigned int sign : 1;
    unsigned int exp : 11;
    unsigned int man_high : 20;
    unsigned int man_low : 32;
  } bits;
};

#if V8_TARGET_LITTLE_ENDIAN
using IeeeDoubleArchType = IeeeDoubleLittleEndianArchType;
constexpr int kIeeeDoubleMantissaWordOffset = 0;
constexpr int kIeeeDoubleExponentWordOffset = 4;
#else
using IeeeDoubleArchType = IeeeDoubleBigEndianArchType;
constexpr int kIeeeDoubleMantissaWordOffset = 4;
constexpr int kIeeeDoubleExponentWordOffset = 0;
#endif

// -----------------------------------------------------------------------------
// Macros

// Testers for test.

#define HAS_SMI_TAG(value) \
  ((static_cast<i::Tagged_t>(value) & ::i::kSmiTagMask) == ::i::kSmiTag)

#define HAS_STRONG_HEAP_OBJECT_TAG(value)                          \
  (((static_cast<i::Tagged_t>(value) & ::i::kHeapObjectTagMask) == \
    ::i::kHeapObjectTag))

#define HAS_WEAK_HEAP_OBJECT_TAG(value)                            \
  (((static_cast<i::Tagged_t>(value) & ::i::kHeapObjectTagMask) == \
    ::i::kWeakHeapObjectTag))

// OBJECT_POINTER_ALIGN returns the value aligned as a HeapObject pointer
#define OBJECT_POINTER_ALIGN(value) \
  (((value) + ::i::kObjectAlignmentMask) & ~::i::kObjectAlignmentMask)

// OBJECT_POINTER_ALIGN is used to statically align object sizes to
// kObjectAlignment (which is kTaggedSize). ALIGN_TO_ALLOCATION_ALIGNMENT is
// used for dynamic allocations to align sizes and addresses to at least 8 bytes
// when an 8GB+ compressed heap is enabled.
// TODO(v8:13070): Consider merging this with OBJECT_POINTER_ALIGN.
#ifdef V8_COMPRESS_POINTERS_8GB
#define ALIGN_TO_ALLOCATION_ALIGNMENT(value)      \
  (((value) + ::i::kObjectAlignment8GbHeapMask) & \
   ~::i::kObjectAlignment8GbHeapMask)
#else
#define ALIGN_TO_ALLOCATION_ALIGNMENT(value) (value)
#endif

// OBJECT_POINTER_PADDING returns the padding size required to align value
// as a HeapObject pointer
#define OBJECT_POINTER_PADDING(value) (OBJECT_POINTER_ALIGN(value) - (value))

// POINTER_SIZE_ALIGN returns the value aligned as a system pointer.
#define POINTER_SIZE_ALIGN(value) \
  (((value) + ::i::kPointerAlignmentMask) & ~::i::kPointerAlignmentMask)

// POINTER_SIZE_PADDING returns the padding size required to align value
// as a system pointer.
#define POINTER_SIZE_PADDING(value) (POINTER_SIZE_ALIGN(value) - (value))

// CODE_POINTER_ALIGN returns the value aligned as a generated code segment.
#define CODE_POINTER_ALIGN(value) \
  (((value) + ::i::kCodeAlignmentMask) & ~::i::kCodeAlignmentMask)

// CODE_POINTER_PADDING returns the padding size required to align value
// as a generated code segment.
#define CODE_POINTER_PADDING(value) (CODE_POINTER_ALIGN(value) - (value))

// DOUBLE_POINTER_ALIGN returns the value algined for double pointers.
#define DOUBLE_POINTER_ALIGN(value) \
  (((value) + ::i::kDoubleAlignmentMask) & ~::i::kDoubleAlignmentMask)

// Prediction hint for branches.
enum class BranchHint : uint8_t { kNone, kTrue, kFalse };

// Defines hints about receiver values based on structural knowledge.
enum class ConvertReceiverMode : unsigned {
  kNullOrUndefined,     // Guaranteed to be null or undefined.
  kNotNullOrUndefined,  // Guaranteed to never be null or undefined.
  kAny,                 // No specific knowledge about receiver.

  kLast = kAny
};

inline size_t hash_value(ConvertReceiverMode mode) {
  return base::bit_cast<unsigned>(mode);
}

inline std::ostream& operator<<(std::ostream& os, ConvertReceiverMode mode) {
  switch (mode) {
    case ConvertReceiverMode::kNullOrUndefined:
      return os << "NULL_OR_UNDEFINED";
    case ConvertReceiverMode::kNotNullOrUndefined:
      return os << "NOT_NULL_OR_UNDEFINED";
    case ConvertReceiverMode::kAny:
      return os << "ANY";
  }
  UNREACHABLE();
}

// Valid hints for the abstract operation OrdinaryToPrimitive,
// implemented according to ES6, section 7.1.1.
enum class OrdinaryToPrimitiveHint { kNumber, kString };

// Valid hints for the abstract operation ToPrimitive,
// implemented according to ES6, section 7.1.1.
enum class ToPrimitiveHint { kDefault, kNumber, kString };

// Defines specifics about arguments object or rest parameter creation.
enum class CreateArgumentsType : uint8_t {
  kMappedArguments,
  kUnmappedArguments,
  kRestParameter
};

inline size_t hash_value(CreateArgumentsType type) {
  return base::bit_cast<uint8_t>(type);
}

inline std::ostream& operator<<(std::ostream& os, CreateArgumentsType type) {
  switch (type) {
    case CreateArgumentsType::kMappedArguments:
      return os << "MAPPED_ARGUMENTS";
    case CreateArgumentsType::kUnmappedArguments:
      return os << "UNMAPPED_ARGUMENTS";
    case CreateArgumentsType::kRestParameter:
      return os << "REST_PARAMETER";
  }
  UNREACHABLE();
}

// Threshold calculated using a microbenckmark.
// https://chromium-review.googlesource.com/c/v8/v8/+/3429210
constexpr int kScopeInfoMaxInlinedLocalNamesSize = 75;

enum ScopeType : uint8_t {
  SCRIPT_SCOPE,        // The top-level scope for a script or a top-level eval.
  REPL_MODE_SCOPE,     // The top-level scope for a repl-mode script.
  CLASS_SCOPE,         // The scope introduced by a class.
  EVAL_SCOPE,          // The top-level scope for an eval source.
  FUNCTION_SCOPE,      // The top-level scope for a function.
  MODULE_SCOPE,        // The scope introduced by a module literal
  CATCH_SCOPE,         // The scope introduced by catch.
  BLOCK_SCOPE,         // The scope introduced by a new block.
  WITH_SCOPE,          // The scope introduced by with.
  SHADOW_REALM_SCOPE,  // Synthetic scope for ShadowRealm NativeContexts.
};

inline std::ostream& operator<<(std::ostream& os, ScopeType type) {
  switch (type) {
    case ScopeType::EVAL_SCOPE:
      return os << "EVAL_SCOPE";
    case ScopeType::FUNCTION_SCOPE:
      return os << "FUNCTION_SCOPE";
    case ScopeType::MODULE_SCOPE:
      return os << "MODULE_SCOPE";
    case ScopeType::SCRIPT_SCOPE:
      return os << "SCRIPT_SCOPE";
    case ScopeType::CATCH_SCOPE:
      return os << "CATCH_SCOPE";
    case ScopeType::BLOCK_SCOPE:
      return os << "BLOCK_SCOPE";
    case ScopeType::CLASS_SCOPE:
      return os << "CLASS_SCOPE";
    case ScopeType::WITH_SCOPE:
      return os << "WITH_SCOPE";
    case ScopeType::SHADOW_REALM_SCOPE:
      return os << "SHADOW_REALM_SCOPE";
    case ScopeType::REPL_MODE_SCOPE:
      return os << "REPL_MODE_SCOPE";
  }
  UNREACHABLE();
}

// AllocationSiteMode controls whether allocations are tracked by an allocation
// site.
enum AllocationSiteMode {
  DONT_TRACK_ALLOCATION_SITE,
  TRACK_ALLOCATION_SITE,
  LAST_ALLOCATION_SITE_MODE = TRACK_ALLOCATION_SITE
};

enum class AllocationSiteUpdateMode { kUpdate, kCheckOnly };

// The mips architecture prior to revision 5 has inverted encoding for sNaN.
#if (V8_TARGET_ARCH_MIPS64 && !defined(_MIPS_ARCH_MIPS64R6) && \
     (!defined(USE_SIMULATOR) || !defined(_MIPS_TARGET_SIMULATOR)))
constexpr uint32_t kHoleNanUpper32 = 0xFFFF7FFF;
constexpr uint32_t kHoleNanLower32 = 0xFFFF7FFF;
#else
constexpr uint32_t kHoleNanUpper32 = 0xFFF7FFFF;
constexpr uint32_t kHoleNanLower32 = 0xFFF7FFFF;
#endif

constexpr uint64_t kHoleNanInt64 =
    (static_cast<uint64_t>(kHoleNanUpper32) << 32) | kHoleNanLower32;

// ES6 section 20.1.2.6 Number.MAX_SAFE_INTEGER
constexpr uint64_t kMaxSafeIntegerUint64 = 9007199254740991;  // 2^53-1
static_assert(kMaxSafeIntegerUint64 == (uint64_t{1} << 53) - 1);
constexpr double kMaxSafeInteger = static_cast<double>(kMaxSafeIntegerUint64);
// ES6 section 21.1.2.8 Number.MIN_SAFE_INTEGER
constexpr double kMinSafeInteger = -kMaxSafeInteger;

constexpr double kMaxUInt32Double = double{kMaxUInt32};

// The order of this enum has to be kept in sync with the predicates below.
enum class VariableMode : uint8_t {
  // User declared variables:
  kLet,  // declared via 'let' declarations (first lexical)

  kConst,  // declared via 'const' declarations

  kUsing,  // declared via 'using' declaration for explicit resource management

  kAwaitUsing,  // declared via 'await using' declaration for explicit resource
                // management
                // (last lexical)

  kVar,  // declared via 'var', and 'function' declarations

  // Variables introduced by the compiler:
  kTemporary,  // temporary variables (not user-visible), stack-allocated
               // unless the scope as a whole has forced context allocation

  kDynamic,  // always require dynamic lookup (we don't know
             // the declaration)

  kDynamicGlobal,  // requires dynamic lookup, but we know that the
                   // variable is global unless it has been shadowed
                   // by an eval-introduced variable

  kDynamicLocal,  // requires dynamic lookup, but we know that the
                  // variable is local and where it is unless it
                  // has been shadowed by an eval-introduced
                  // variable

  // Variables for private methods or accessors whose access require
  // brand check. Declared only in class scopes by the compiler
  // and allocated only in class contexts:
  kPrivateMethod,  // Does not coexist with any other variable with the same
                   // name in the same scope.

  kPrivateSetterOnly,  // Incompatible with variables with the same name but
                       // any mode other than kPrivateGetterOnly. Transition to
                       // kPrivateGetterAndSetter if a later declaration for the
                       // same name with kPrivateGetterOnly is made.

  kPrivateGetterOnly,  // Incompatible with variables with the same name but
                       // any mode other than kPrivateSetterOnly. Transition to
                       // kPrivateGetterAndSetter if a later declaration for the
                       // same name with kPrivateSetterOnly is made.

  kPrivateGetterAndSetter,  // Does not coexist with any other variable with the
                            // same name in the same scope.

  kLastLexicalVariableMode = kAwaitUsing,
};

// Printing support
#ifdef DEBUG
inline const char* VariableMode2String(VariableMode mode) {
  switch (mode) {
    case VariableMode::kVar:
      return "VAR";
    case VariableMode::kLet:
      return "LET";
    case VariableMode::kPrivateGetterOnly:
      return "PRIVATE_GETTER_ONLY";
    case VariableMode::kPrivateSetterOnly:
      return "PRIVATE_SETTER_ONLY";
    case VariableMode::kPrivateMethod:
      return "PRIVATE_METHOD";
    case VariableMode::kPrivateGetterAndSetter:
      return "PRIVATE_GETTER_AND_SETTER";
    case VariableMode::kConst:
      return "CONST";
    case VariableMode::kDynamic:
      return "DYNAMIC";
    case VariableMode::kDynamicGlobal:
      return "DYNAMIC_GLOBAL";
    case VariableMode::kDynamicLocal:
      return "DYNAMIC_LOCAL";
    case VariableMode::kTemporary:
      return "TEMPORARY";
    case VariableMode::kUsing:
      return "USING";
    case VariableMode::kAwaitUsing:
      return "AWAIT_USING";
  }
  UNREACHABLE();
}
#endif

enum VariableKind : uint8_t {
  NORMAL_VARIABLE,
  PARAMETER_VARIABLE,
  THIS_VARIABLE,
  SLOPPY_BLOCK_FUNCTION_VARIABLE,
  SLOPPY_FUNCTION_NAME_VARIABLE
};

inline bool IsDynamicVariableMode(VariableMode mode) {
  return mode >= VariableMode::kDynamic && mode <= VariableMode::kDynamicLocal;
}

inline bool IsDeclaredVariableMode(VariableMode mode) {
  static_assert(static_cast<uint8_t>(VariableMode::kLet) ==
                0);  // Implies that mode >= VariableMode::kLet.
  return mode <= VariableMode::kVar;
}

inline bool IsPrivateAccessorVariableMode(VariableMode mode) {
  return mode >= VariableMode::kPrivateSetterOnly &&
         mode <= VariableMode::kPrivateGetterAndSetter;
}

inline bool IsPrivateMethodVariableMode(VariableMode mode) {
  return mode == VariableMode::kPrivateMethod;
}

inline bool IsPrivateMethodOrAccessorVariableMode(VariableMode mode) {
  return IsPrivateMethodVariableMode(mode) ||
         IsPrivateAccessorVariableMode(mode);
}

inline bool IsSerializableVariableMode(VariableMode mode) {
  return IsDeclaredVariableMode(mode) ||
         IsPrivateMethodOrAccessorVariableMode(mode);
}

inline bool IsImmutableLexicalVariableMode(VariableMode mode) {
  return mode == VariableMode::kConst || mode == VariableMode::kUsing ||
         mode == VariableMode::kAwaitUsing;
}

inline bool IsImmutableLexicalOrPrivateVariableMode(VariableMode mode) {
  return IsImmutableLexicalVariableMode(mode) ||
         IsPrivateMethodOrAccessorVariableMode(mode);
}

inline bool IsLexicalVariableMode(VariableMode mode) {
  static_assert(static_cast<uint8_t>(VariableMode::kLet) ==
                0);  // Implies that mode >= VariableMode::kLet.
  return mode <= VariableMode::kLastLexicalVariableMode;
}

enum VariableLocation : uint8_t {
  // Before and during variable allocation, a variable whose location is
  // not yet determined.  After allocation, a variable looked up as a
  // property on the global object (and possibly absent).  name() is the
  // variable name, index() is invalid.
  UNALLOCATED,

  // A slot in the parameter section on the stack.  index() is the
  // parameter index, counting left-to-right.  The receiver is index -1;
  // the first parameter is index 0.
  PARAMETER,

  // A slot in the local section on the stack.  index() is the variable
  // index in the stack frame, starting at 0.
  LOCAL,

  // An indexed slot in a heap context.  index() is the variable index in
  // the context object on the heap, starting at 0.  scope() is the
  // corresponding scope.
  CONTEXT,

  // A named slot in a heap context.  name() is the variable name in the
  // context object on the heap, with lookup starting at the current
  // context.  index() is invalid.
  LOOKUP,

  // A named slot in a module's export table.
  MODULE,

  // An indexed slot in a script context. index() is the variable
  // index in the context object on the heap, starting at 0.
  // Important: REPL_GLOBAL variables from different scripts with the
  //            same name share a single script context slot. Every
  //            script context will reserve a slot, but only one will be used.
  // REPL_GLOBAL variables are stored in script contexts, but accessed like
  // globals, i.e. they always require a lookup at runtime to find the right
  // script context.
  REPL_GLOBAL,

  kLastVariableLocation = REPL_GLOBAL
};

// ES6 specifies declarative environment records with mutable and immutable
// bindings that can be in two states: initialized and uninitialized.
// When accessing a binding, it needs to be checked for initialization.
// However in the following cases the binding is initialized immediately
// after creation so the initialization check can always be skipped:
//
// 1. Var declared local variables.
//      var foo;
// 2. A local variable introduced by a function declaration.
//      function foo() {}
// 3. Parameters
//      function x(foo) {}
// 4. Catch bound variables.
//      try {} catch (foo) {}
// 6. Function name variables of named function expressions.
//      var x = function foo() {}
// 7. Implicit binding of 'this'.
// 8. Implicit binding of 'arguments' in functions.
//
// The following enum specifies a flag that indicates if the binding needs a
// distinct initialization step (kNeedsInitialization) or if the binding is
// immediately initialized upon creation (kCreatedInitialized).
enum InitializationFlag : uint8_t { kNeedsInitialization, kCreatedInitialized };

// Static variables can only be used with the class in the closest
// class scope as receivers.
enum class IsStaticFlag : uint8_t { kNotStatic, kStatic };

enum MaybeAssignedFlag : uint8_t { kNotAssigned, kMaybeAssigned };

enum class InterpreterPushArgsMode : unsigned {
  kArrayFunction,
  kWithFinalSpread,
  kOther
};

inline size_t hash_value(InterpreterPushArgsMode mode) {
  return base::bit_cast<unsigned>(mode);
}

inline std::ostream& operator<<(std::ostream& os,
                                InterpreterPushArgsMode mode) {
  switch (mode) {
    case InterpreterPushArgsMode::kArrayFunction:
      return os << "ArrayFunction";
    case InterpreterPushArgsMode::kWithFinalSpread:
      return os << "WithFinalSpread";
    case InterpreterPushArgsMode::kOther:
      return os << "Other";
  }
  UNREACHABLE();
}

inline uint32_t ObjectHash(Address address) {
  // All objects are at least pointer aligned, so we can remove the trailing
  // zeros.
  return static_cast<uint32_t>(address >> kTaggedSizeLog2);
}

// Type feedback is encoded in such a way that, we can combine the feedback
// at different points by performing an 'OR' operation. Type feedback moves
// to a more generic type when we combine feedback.
//
//   kSignedSmall -> kSignedSmallInputs -> kNumber  -> kNumberOrOddball -> kAny
//                                                     kString          -> kAny
//                                        kBigInt64 -> kBigInt          -> kAny
//
// Technically we wouldn't need the separation between the kNumber and the
// kNumberOrOddball values here, since for binary operations, we always
// truncate oddballs to numbers. In practice though it causes TurboFan to
// generate quite a lot of unused code though if we always handle numbers
// and oddballs everywhere, although in 99% of the use sites they are only
// used with numbers.
class BinaryOperationFeedback {
 public:
  enum {
    kNone = 0x0,
    kSignedSmall = 0x1,
    kSignedSmallInputs = 0x3,
    kNumber = 0x7,
    kNumberOrOddball = 0xF,
    kString = 0x10,
    kBigInt64 = 0x20,
    kBigInt = 0x60,
    kStringWrapper = 0x80,
    kStringOrStringWrapper = 0x90,
    kAny = 0x7F
  };
};

// Type feedback is encoded in such a way that, we can combine the feedback
// at different points by performing an 'OR' operation.
// This is distinct from BinaryOperationFeedback on purpose, because the
// feedback that matters differs greatly as well as the way it is consumed.
class CompareOperationFeedback {
  enum {
    kSignedSmallFlag = 1 << 0,
    kOtherNumberFlag = 1 << 1,
    kBooleanFlag = 1 << 2,
    kNullOrUndefinedFlag = 1 << 3,
    kInternalizedStringFlag = 1 << 4,
    kOtherStringFlag = 1 << 5,
    kSymbolFlag = 1 << 6,
    kBigInt64Flag = 1 << 7,
    kOtherBigIntFlag = 1 << 8,
    kReceiverFlag = 1 << 9,
    kAnyMask = 0x3FF,
  };

 public:
  enum Type {
    kNone = 0,

    kBoolean = kBooleanFlag,
    kNullOrUndefined = kNullOrUndefinedFlag,
    kOddball = kBoolean | kNullOrUndefined,

    kSignedSmall = kSignedSmallFlag,
    kNumber = kSignedSmall | kOtherNumberFlag,
    kNumberOrBoolean = kNumber | kBoolean,
    kNumberOrOddball = kNumber | kOddball,

    kInternalizedString = kInternalizedStringFlag,
    kString = kInternalizedString | kOtherStringFlag,

    kReceiver = kReceiverFlag,
    kReceiverOrNullOrUndefined = kReceiver | kNullOrUndefined,

    kBigInt64 = kBigInt64Flag,
    kBigInt = kBigInt64Flag | kOtherBigIntFlag,
    kSymbol = kSymbolFlag,

    kAny = kAnyMask,
  };
};

class TypeOfFeedback {
  enum {
    kNumberFlag = 1,
    kFunctionFlag = 1 << 1,
    kStringFlag = 1 << 2,
  };

 public:
  enum Result {
    kNone = 0,
    kNumber = kNumberFlag,
    kFunction = kFunctionFlag,
    kString = kStringFlag,
    kAny = kNumberFlag | kFunctionFlag | kStringFlag,
  };
};

// Type feedback is encoded in such a way that, we can combine the feedback
// at different points by performing an 'OR' operation. Type feedback moves
// to a more generic type when we combine feedback.
// kNone -> kEnumCacheKeysAndIndices -> kEnumCacheKeys -> kAny
enum class ForInFeedback : uint8_t {
  kNone = 0x0,
  kEnumCacheKeysAndIndices = 0x1,
  kEnumCacheKeys = 0x3,
  kAny = 0x7
};
static_assert((static_cast<int>(ForInFeedback::kNone) |
               static_cast<int>(ForInFeedback::kEnumCacheKeysAndIndices)) ==
              static_cast<int>(ForInFeedback::kEnumCacheKeysAndIndices));
static_assert((static_cast<int>(ForInFeedback::kEnumCacheKeysAndIndices) |
               static_cast<int>(ForInFeedback::kEnumCacheKeys)) ==
              static_cast<int>(ForInFeedback::kEnumCacheKeys));
static_assert((static_cast<int>(ForInFeedback::kEnumCacheKeys) |
               static_cast<int>(ForInFeedback::kAny)) ==
              static_cast<int>(ForInFeedback::kAny));

enum class UnicodeEncoding : uint8_t {
  // Different unicode encodings in a |word32|:
  UTF16,  // hi 16bits -> trailing surrogate or 0, low 16bits -> lead surrogate
  UTF32,  // full UTF32 code unit / Unicode codepoint
};

inline size_t hash_value(UnicodeEncoding encoding) {
  return static_cast<uint8_t>(encoding);
}

inline std::ostream& operator<<(std::ostream& os, UnicodeEncoding encoding) {
  switch (encoding) {
    case UnicodeEncoding::UTF16:
      return os << "UTF16";
    case UnicodeEncoding::UTF32:
      return os << "UTF32";
  }
  UNREACHABLE();
}

enum class IterationKind { kKeys, kValues, kEntries };

inline std::ostream& operator<<(std::ostream& os, IterationKind kind) {
  switch (kind) {
    case IterationKind::kKeys:
      return os << "IterationKind::kKeys";
    case IterationKind::kValues:
      return os << "IterationKind::kValues";
    case IterationKind::kEntries:
      return os << "IterationKind::kEntries";
  }
  UNREACHABLE();
}

enum class CollectionKind { kMap, kSet };

inline std::ostream& operator<<(std::ostream& os, CollectionKind kind) {
  switch (kind) {
    case CollectionKind::kMap:
      return os << "CollectionKind::kMap";
    case CollectionKind::kSet:
      return os << "CollectionKind::kSet";
  }
  UNREACHABLE();
}

enum class IsolateExecutionModeFlag : uint8_t {
  // Default execution mode.
  kNoFlags = 0,
  // Set if the Isolate is being profiled. Causes collection of extra compile
  // info.
  kIsProfiling = 1 << 0,
  // Set if side effect checking is enabled for the Isolate.
  // See Debug::StartSideEffectCheckMode().
  kCheckSideEffects = 1 << 1,
};

// Flags for the runtime function kDefineKeyedOwnPropertyInLiteral.
// - Whether the function name should be set or not.
enum class DefineKeyedOwnPropertyInLiteralFlag {
  kNoFlags = 0,
  kSetFunctionName = 1 << 0
};
using DefineKeyedOwnPropertyInLiteralFlags =
    base::Flags<DefineKeyedOwnPropertyInLiteralFlag>;
DEFINE_OPERATORS_FOR_FLAGS(DefineKeyedOwnPropertyInLiteralFlags)

enum class DefineKeyedOwnPropertyFlag {
  kNoFlags = 0,
  kSetFunctionName = 1 << 0
};
using DefineKeyedOwnPropertyFlags = base::Flags<DefineKeyedOwnPropertyFlag>;
DEFINE_OPERATORS_FOR_FLAGS(DefineKeyedOwnPropertyFlags)

enum ExternalArrayType {
  kExternalInt8Array = 1,
  kExternalUint8Array,
  kExternalInt16Array,
  kExternalUint16Array,
  kExternalInt32Array,
  kExternalUint32Array,
  kExternalFloat16Array,
  kExternalFloat32Array,
  kExternalFloat64Array,
  kExternalUint8ClampedArray,
  kExternalBigInt64Array,
  kExternalBigUint64Array,
};

struct AssemblerDebugInfo {
  AssemblerDebugInfo(const char* name, const char* file, int line)
      : name(name), file(file), line(line) {}
  const char* name;
  const char* file;
  int line;
};

inline std::ostream& operator<<(std::ostream& os,
                                const AssemblerDebugInfo& info) {
  os << "(" << info.name << ":" << info.file << ":" << info.line << ")";
  return os;
}

using FileAndLine = std::pair<const char*, int>;

#define TIERING_STATE_LIST(V)           \
  V(None, 0b000)                        \
  V(InProgress, 0b001)                  \
  V(RequestMaglev_Synchronous, 0b010)   \
  V(RequestMaglev_Concurrent, 0b011)    \
  V(RequestTurbofan_Synchronous, 0b100) \
  V(RequestTurbofan_Concurrent, 0b101)

enum class TieringState : int32_t {
#define V(Name, Value) k##Name = Value,
  TIERING_STATE_LIST(V)
#undef V
      kLastTieringState = kRequestTurbofan_Concurrent,
};

// The state kInProgress (= an optimization request for this function is
// currently being serviced) currently means that no other tiering action can
// happen. Define this constant so we can static_assert it at related code
// sites.
static constexpr bool kTieringStateInProgressBlocksTierup = true;

// To efficiently check whether a marker is kNone or kInProgress using a single
// mask, we expect the kNone to be 0 and kInProgress to be 1 so that we can
// mask off the lsb for checking.
static_assert(static_cast<int>(TieringState::kNone) == 0b00 &&
              static_cast<int>(TieringState::kInProgress) == 0b01);
static_assert(static_cast<int>(TieringState::kLastTieringState) <= 0b111);
static constexpr uint32_t kNoneOrInProgressMask = 0b110;

#define V(Name, Value)                          \
  constexpr bool Is##Name(TieringState state) { \
    return state == TieringState::k##Name;      \
  }
TIERING_STATE_LIST(V)
#undef V

constexpr bool IsRequestMaglev(TieringState state) {
  return IsRequestMaglev_Concurrent(state) ||
         IsRequestMaglev_Synchronous(state);
}
constexpr bool IsRequestTurbofan(TieringState state) {
  return IsRequestTurbofan_Concurrent(state) ||
         IsRequestTurbofan_Synchronous(state);
}

constexpr const char* ToString(TieringState marker) {
  switch (marker) {
#define V(Name, Value)        \
  case TieringState::k##Name: \
    return "TieringState::k" #Name;
    TIERING_STATE_LIST(V)
#undef V
  }
}

inline std::ostream& operator<<(std::ostream& os, TieringState marker) {
  return os << ToString(marker);
}

#undef TIERING_STATE_LIST

// State machine:
// S(tate)0: kPending
// S1: kEarlyMaglev
// S2: kEarlyTurbofan
// S3: kNormal
//
// C(ondition)0: maglev compile
// C1: ic was stable early
// C2: turbofan compile
// C3: ic change or deopt
//
// S0 -- C0 -- C1 --> S1 -- C2 -- C3 --> S2 --|
//             |      |                       |
//             |      |-----------------------|
//             |                 |
//             |                 C3
//             |                 |
//             |-----------------------> S3
enum class CachedTieringDecision : int32_t {
  kPending,
  kEarlyMaglev,
  kEarlyTurbofan,
  kNormal,
};

enum class SpeculationMode { kAllowSpeculation, kDisallowSpeculation };
enum class CallFeedbackContent { kTarget, kReceiver };

inline std::ostream& operator<<(std::ostream& os,
                                SpeculationMode speculation_mode) {
  switch (speculation_mode) {
    case SpeculationMode::kAllowSpeculation:
      return os << "SpeculationMode::kAllowSpeculation";
    case SpeculationMode::kDisallowSpeculation:
      return os << "SpeculationMode::kDisallowSpeculation";
  }
}

enum class BlockingBehavior { kBlock, kDontBlock };

enum class ConcurrencyMode : uint8_t { kSynchronous, kConcurrent };

constexpr bool IsSynchronous(ConcurrencyMode mode) {
  return mode == ConcurrencyMode::kSynchronous;
}
constexpr bool IsConcurrent(ConcurrencyMode mode) {
  return mode == ConcurrencyMode::kConcurrent;
}

constexpr const char* ToString(ConcurrencyMode mode) {
  switch (mode) {
    case ConcurrencyMode::kSynchronous:
      return "ConcurrencyMode::kSynchronous";
    case ConcurrencyMode::kConcurrent:
      return "ConcurrencyMode::kConcurrent";
  }
}
inline std::ostream& operator<<(std::ostream& os, ConcurrencyMode mode) {
  return os << ToString(mode);
}

// An architecture independent representation of the sets of registers available
// for instruction creation.
enum class AliasingKind {
  // Registers alias a single register of every other size (e.g. Intel).
  kOverlap,
  // Registers alias two registers of the next smaller size (e.g. ARM).
  kCombine,
  // SIMD128 Registers are independent of every other size (e.g Riscv)
  kIndependent
};

#define FOR_EACH_ISOLATE_ADDRESS_NAME(C)                            \
  C(Handler, handler)                                               \
  C(CEntryFP, c_entry_fp)                                           \
  C(CFunction, c_function)                                          \
  C(Context, context)                                               \
  C(Exception, exception)                                           \
  C(TopmostScriptHavingContext, topmost_script_having_context)      \
  C(PendingHandlerContext, pending_handler_context)                 \
  C(PendingHandlerEntrypoint, pending_handler_entrypoint)           \
  C(PendingHandlerConstantPool, pending_handler_constant_pool)      \
  C(PendingHandlerFP, pending_handler_fp)                           \
  C(PendingHandlerSP, pending_handler_sp)                           \
  C(NumFramesAbovePendingHandler, num_frames_above_pending_handler) \
  C(IsOnCentralStackFlag, is_on_central_stack_flag)                 \
  C(JSEntrySP, js_entry_sp)

enum IsolateAddressId {
#define DECLARE_ENUM(CamelName, hacker_name) k##CamelName##Address,
  FOR_EACH_ISOLATE_ADDRESS_NAME(DECLARE_ENUM)
#undef DECLARE_ENUM
      kIsolateAddressCount
};

// The reason for a WebAssembly trap.
#define FOREACH_WASM_TRAPREASON(V) \
  V(TrapUnreachable)               \
  V(TrapMemOutOfBounds)            \
  V(TrapUnalignedAccess)           \
  V(TrapDivByZero)                 \
  V(TrapDivUnrepresentable)        \
  V(TrapRemByZero)                 \
  V(TrapFloatUnrepresentable)      \
  V(TrapFuncSigMismatch)           \
  V(TrapDataSegmentOutOfBounds)    \
  V(TrapElementSegmentOutOfBounds) \
  V(TrapTableOutOfBounds)          \
  V(TrapRethrowNull)               \
  V(TrapNullDereference)           \
  V(TrapIllegalCast)               \
  V(TrapArrayOutOfBounds)          \
  V(TrapArrayTooLarge)             \
  V(TrapStringOffsetOutOfBounds)

enum class KeyedAccessLoadMode {
  kInBounds = 0b00,
  kHandleOOB = 0b01,
  kHandleHoles = 0b10,
  kHandleOOBAndHoles = 0b11,
};

inline KeyedAccessLoadMode CreateKeyedAccessLoadMode(bool handle_oob,
                                                     bool handle_holes) {
  return static_cast<KeyedAccessLoadMode>(
      static_cast<int>(handle_oob) | (static_cast<int>(handle_holes) << 1));
}

inline KeyedAccessLoadMode GeneralizeKeyedAccessLoadMode(
    KeyedAccessLoadMode mode1, KeyedAccessLoadMode mode2) {
  using T = std::underlying_type<KeyedAccessLoadMode>::type;
  return static_cast<KeyedAccessLoadMode>(static_cast<T>(mode1) |
                                          static_cast<T>(mode2));
}

inline bool LoadModeHandlesOOB(KeyedAccessLoadMode load_mode) {
  using T = std::underlying_type<KeyedAccessLoadMode>::type;
  return (static_cast<T>(load_mode) &
          static_cast<T>(KeyedAccessLoadMode::kHandleOOB)) != 0;
}

inline bool LoadModeHandlesHoles(KeyedAccessLoadMode load_mode) {
  using T = std::underlying_type<KeyedAccessLoadMode>::type;
  return (static_cast<T>(load_mode) &
          static_cast<T>(KeyedAccessLoadMode::kHandleHoles)) != 0;
}

enum class KeyedAccessStoreMode {
  kInBounds,
  kGrowAndHandleCOW,
  kIgnoreTypedArrayOOB,
  kHandleCOW,
};

inline std::ostream& operator<<(std::ostream& os, KeyedAccessStoreMode mode) {
  switch (mode) {
    case KeyedAccessStoreMode::kInBounds:
      return os << "kInBounds";
    case KeyedAccessStoreMode::kGrowAndHandleCOW:
      return os << "kGrowAndHandleCOW";
    case KeyedAccessStoreMode::kIgnoreTypedArrayOOB:
      return os << "kIgnoreTypedArrayOOB";
    case KeyedAccessStoreMode::kHandleCOW:
      return os << "kHandleCOW";
  }
  UNREACHABLE();
}

enum MutableMode { MUTABLE, IMMUTABLE };

inline bool StoreModeIsInBounds(KeyedAccessStoreMode store_mode) {
  return store_mode == KeyedAccessStoreMode::kInBounds;
}

inline bool StoreModeHandlesCOW(KeyedAccessStoreMode store_mode) {
  return store_mode == KeyedAccessStoreMode::kHandleCOW ||
         store_mode == KeyedAccessStoreMode::kGrowAndHandleCOW;
}

inline bool StoreModeSupportsTypeArray(KeyedAccessStoreMode store_mode) {
  return store_mode == KeyedAccessStoreMode::kInBounds ||
         store_mode == KeyedAccessStoreMode::kIgnoreTypedArrayOOB;
}

inline bool StoreModeIgnoresTypeArrayOOB(KeyedAccessStoreMode store_mode) {
  return store_mode == KeyedAccessStoreMode::kIgnoreTypedArrayOOB;
}

inline bool StoreModeCanGrow(KeyedAccessStoreMode store_mode) {
  return store_mode == KeyedAccessStoreMode::kGrowAndHandleCOW;
}

enum class IcCheckType { kElement, kProperty };

// Helper stubs can be called in different ways depending on where the target
// code is located and how the call sequence is expected to look like:
//  - CodeObject: Call on-heap {Code} object via {RelocInfo::CODE_TARGET}.
//  - WasmRuntimeStub: Call native {WasmCode} stub via
//    {RelocInfo::WASM_STUB_CALL}.
//  - BuiltinPointer: Call a builtin based on a builtin pointer with dynamic
//    contents. If builtins are embedded, we call directly into off-heap code
//    without going through the on-heap Code trampoline.
enum class StubCallMode {
  kCallCodeObject,
#if V8_ENABLE_WEBASSEMBLY
  kCallWasmRuntimeStub,
#endif  // V8_ENABLE_WEBASSEMBLY
  kCallBuiltinPointer,
};

enum class NeedsContext { kYes, kNo };

constexpr int kInvalidInfoId = -1;
constexpr int kFunctionLiteralIdTopLevel = 0;

constexpr int kSwissNameDictionaryInitialCapacity = 4;

constexpr int kSmallOrderedHashSetMinCapacity = 4;
constexpr int kSmallOrderedHashMapMinCapacity = 4;

constexpr int kJSArgcReceiverSlots = 1;
constexpr uint16_t kDontAdaptArgumentsSentinel = 0;

// Helper to get the parameter count for functions with JS linkage.
inline constexpr int JSParameterCount(int param_count_without_receiver) {
  return param_count_without_receiver + kJSArgcReceiverSlots;
}

// A special {Parameter} index for JSCalls that represents the closure.
// The constant is defined here for accessibility (without having to include TF
// internals), even though it is mostly relevant to Turbofan.
constexpr int kJSCallClosureParameterIndex = -1;
constexpr int kMinParameterIndex = kJSCallClosureParameterIndex;

// Opaque data type for identifying stack frames. Used extensively
// by the debugger.
// ID_MIN_VALUE and ID_MAX_VALUE are specified to ensure that enumeration type
// has correct value range (see Issue 830 for more details).
enum StackFrameId { ID_MIN_VALUE = kMinInt, ID_MAX_VALUE = kMaxInt, NO_ID = 0 };

enum class ExceptionStatus : bool { kException = false, kSuccess = true };
V8_INLINE bool operator!(ExceptionStatus status) {
  return !static_cast<bool>(status);
}

// Used in the ScopeInfo flags fields for the function name variable for named
// function expressions, and for the receiver. Must be declared here so that it
// can be used in Torque.
enum class VariableAllocationInfo { NONE, STACK, CONTEXT, UNUSED };

#ifdef V8_COMPRESS_POINTERS
class PtrComprCageBase {
 public:
  explicit constexpr PtrComprCageBase(Address address) : address_(address) {}
  // NOLINTNEXTLINE
  inline PtrComprCageBase(const Isolate* isolate);
  // NOLINTNEXTLINE
  inline PtrComprCageBase(const LocalIsolate* isolate);

  inline Address address() const { return address_; }

  bool operator==(const PtrComprCageBase& other) const {
    return address_ == other.address_;
  }

 private:
  Address address_;
};
#else
class PtrComprCageBase {
 public:
  explicit constexpr PtrComprCageBase(Address address) {}
  PtrComprCageBase() = default;
  // NOLINTNEXTLINE
  PtrComprCageBase(const Isolate* isolate) {}
  // NOLINTNEXTLINE
  PtrComprCageBase(const LocalIsolate* isolate) {}
};
#endif

class int31_t {
 public:
  constexpr int31_t() : value_(0) {}
  constexpr int31_t(int value) : value_(value) {  // NOLINT(runtime/explicit)
    DCHECK_EQ((value & 0x80000000) != 0, (value & 0x40000000) != 0);
  }
  int31_t& operator=(int value) {
    DCHECK_EQ((value & 0x80000000) != 0, (value & 0x40000000) != 0);
    value_ = value;
    return *this;
  }
  int32_t value() const { return value_; }
  operator int32_t() const { return value_; }

 private:
  int32_t value_;
};

enum PropertiesEnumerationMode {
  // String and then Symbol properties according to the spec
  // ES#sec-object.assign
  kEnumerationOrder,
  // Order of property addition
  kPropertyAdditionOrder,
};

enum class StringTransitionStrategy {
  // The string must be transitioned to a new representation by first copying.
  kCopy,
  // The string can be transitioned in-place by changing its map.
  kInPlace,
  // The string is already transitioned to the desired representation.
  kAlreadyTransitioned
};

}  // namespace internal

// Tag dispatching support for atomic loads and stores.
struct AcquireLoadTag {};
struct RelaxedLoadTag {};
struct ReleaseStoreTag {};
struct RelaxedStoreTag {};
struct SeqCstAccessTag {};
static constexpr AcquireLoadTag kAcquireLoad;
static constexpr RelaxedLoadTag kRelaxedLoad;
static constexpr ReleaseStoreTag kReleaseStore;
static constexpr RelaxedStoreTag kRelaxedStore;
static constexpr SeqCstAccessTag kSeqCstAccess;

}  // namespace v8

namespace i = v8::internal;

#endif  // V8_COMMON_GLOBALS_H_
