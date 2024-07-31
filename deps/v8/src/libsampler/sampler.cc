// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libsampler/sampler.h"

#include "include/v8-isolate.h"
#include "include/v8-platform.h"
#include "include/v8-unwinder.h"

#ifdef USE_SIGNALS

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <atomic>

#if !V8_OS_QNX && !V8_OS_AIX
#include <sys/syscall.h>
#endif

#if V8_OS_AIX || V8_TARGET_ARCH_S390X

#include "src/base/platform/time.h"

#elif V8_OS_DARWIN
#include <mach/mach.h>
// OpenBSD doesn't have <ucontext.h>. ucontext_t lives in <signal.h>
// and is a typedef for struct sigcontext. There is no uc_mcontext.
#elif !V8_OS_OPENBSD
#include <ucontext.h>
#endif

#include <unistd.h>

#elif V8_OS_WIN || V8_OS_CYGWIN

#include <windows.h>

#include "src/base/win32-headers.h"

#elif V8_OS_FUCHSIA

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

// TODO(wez): Remove this once the Fuchsia SDK has rolled.
#if defined(ZX_THREAD_STATE_REGSET0)
#define ZX_THREAD_STATE_GENERAL_REGS ZX_THREAD_STATE_REGSET0
zx_status_t zx_thread_read_state(zx_handle_t h, uint32_t k, void* b, size_t l) {
  uint32_t dummy_out_len = 0;
  return zx_thread_read_state(h, k, b, static_cast<uint32_t>(l),
                              &dummy_out_len);
}
#if defined(__x86_64__)
using zx_thread_state_general_regs_t = zx_x86_64_general_regs_t;
#else
using zx_thread_state_general_regs_t = zx_arm64_general_regs_t;
#endif
#endif  // !defined(ZX_THREAD_STATE_GENERAL_REGS)

#endif

#include <algorithm>
#include <vector>

#include "src/base/atomic-utils.h"
#include "src/base/platform/platform.h"

#if V8_OS_ANDROID && !defined(__BIONIC_HAVE_UCONTEXT_T)

// Not all versions of Android's C library provide ucontext_t.
// Detect this and provide custom but compatible definitions. Note that these
// follow the GLibc naming convention to access register values from
// mcontext_t.
//
// See http://code.google.com/p/android/issues/detail?id=34784

#if defined(__arm__)

using mcontext_t = struct sigcontext;

struct ucontext_t {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
};

#elif defined(__aarch64__)

using mcontext_t = struct sigcontext;

struct ucontext_t {
  uint64_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
};

#elif defined(__mips__)
// MIPS version of sigcontext, for Android bionic.
struct mcontext_t {
  uint32_t regmask;
  uint32_t status;
  uint64_t pc;
  uint64_t gregs[32];
  uint64_t fpregs[32];
  uint32_t acx;
  uint32_t fpc_csr;
  uint32_t fpc_eir;
  uint32_t used_math;
  uint32_t dsp;
  uint64_t mdhi;
  uint64_t mdlo;
  uint32_t hi1;
  uint32_t lo1;
  uint32_t hi2;
  uint32_t lo2;
  uint32_t hi3;
  uint32_t lo3;
};

struct ucontext_t {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
};

#elif defined(__i386__)
// x86 version for Android.
struct mcontext_t {
  uint32_t gregs[19];
  void* fpregs;
  uint32_t oldmask;
  uint32_t cr2;
};

using kernel_sigset_t = uint32_t[2];  // x86 kernel uses 64-bit signal masks
struct ucontext_t {
  uint32_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
};
enum { REG_EBP = 6, REG_ESP = 7, REG_EIP = 14 };

#elif defined(__x86_64__)
// x64 version for Android.
struct mcontext_t {
  uint64_t gregs[23];
  void* fpregs;
  uint64_t __reserved1[8];
};

struct ucontext_t {
  uint64_t uc_flags;
  struct ucontext* uc_link;
  stack_t uc_stack;
  mcontext_t uc_mcontext;
  // Other fields are not used by V8, don't define them here.
};
enum { REG_RBP = 10, REG_RSP = 15, REG_RIP = 16 };
#endif

#endif  // V8_OS_ANDROID && !defined(__BIONIC_HAVE_UCONTEXT_T)

namespace v8 {
namespace sampler {

#if defined(USE_SIGNALS)

AtomicGuard::AtomicGuard(AtomicMutex* atomic, bool is_blocking)
    : atomic_(atomic), is_success_(false) {
  do {
    bool expected = false;
    // We have to use the strong version here for the case where is_blocking
    // is false, and we will only attempt the exchange once.
    is_success_ = atomic->compare_exchange_strong(expected, true);
  } while (is_blocking && !is_success_);
}

AtomicGuard::~AtomicGuard() {
  if (!is_success_) return;
  atomic_->store(false);
}

bool AtomicGuard::is_success() const { return is_success_; }

class Sampler::PlatformData {
 public:
  PlatformData() : vm_tid_(pthread_self()) {}
  pthread_t vm_tid() const { return vm_tid_; }

 private:
  pthread_t vm_tid_;
};

void SamplerManager::AddSampler(Sampler* sampler) {
  AtomicGuard atomic_guard(&samplers_access_counter_);
  DCHECK(sampler->IsActive());
  pthread_t thread_id = sampler->platform_data()->vm_tid();
  auto it = sampler_map_.find(thread_id);
  if (it == sampler_map_.end()) {
    SamplerList samplers;
    samplers.push_back(sampler);
    sampler_map_.emplace(thread_id, std::move(samplers));
  } else {
    SamplerList& samplers = it->second;
    auto sampler_it = std::find(samplers.begin(), samplers.end(), sampler);
    if (sampler_it == samplers.end()) samplers.push_back(sampler);
  }
}

void SamplerManager::RemoveSampler(Sampler* sampler) {
  AtomicGuard atomic_guard(&samplers_access_counter_);
  DCHECK(sampler->IsActive());
  pthread_t thread_id = sampler->platform_data()->vm_tid();
  auto it = sampler_map_.find(thread_id);
  DCHECK_NE(it, sampler_map_.end());
  SamplerList& samplers = it->second;
  samplers.erase(std::remove(samplers.begin(), samplers.end(), sampler),
                 samplers.end());
  if (samplers.empty()) {
    sampler_map_.erase(it);
  }
}

void SamplerManager::DoSample(const v8::RegisterState& state) {
  AtomicGuard atomic_guard(&samplers_access_counter_, false);
  // TODO(petermarshall): Add stat counters for the bailouts here.
  if (!atomic_guard.is_success()) return;
  pthread_t thread_id = pthread_self();
  auto it = sampler_map_.find(thread_id);
  if (it == sampler_map_.end()) return;
  SamplerList& samplers = it->second;

  for (Sampler* sampler : samplers) {
    if (!sampler->ShouldRecordSample()) continue;
    Isolate* isolate = sampler->isolate();
    // We require a fully initialized and entered isolate.
    if (isolate == nullptr || !isolate->IsInUse()) continue;
    sampler->SampleStack(state);
  }
}

SamplerManager* SamplerManager::instance() {
  static base::LeakyObject<SamplerManager> instance;
  return instance.get();
}

#elif V8_OS_WIN || V8_OS_CYGWIN

// ----------------------------------------------------------------------------
// Win32 profiler support. On Cygwin we use the same sampler implementation as
// on Win32.

class Sampler::PlatformData {
 public:
  // Get a handle to the calling thread. This is the thread that we are
  // going to profile. We need to make a copy of the handle because we are
  // going to use it in the sampler thread.
  PlatformData() {
    HANDLE current_process = GetCurrentProcess();
    BOOL result = DuplicateHandle(
        current_process, GetCurrentThread(), current_process, &profiled_thread_,
        THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE, 0);
    DCHECK(result);
    USE(result);
  }

  ~PlatformData() {
    if (profiled_thread_ != nullptr) {
      CloseHandle(profiled_thread_);
      profiled_thread_ = nullptr;
    }
  }

  HANDLE profiled_thread() { return profiled_thread_; }

 private:
  HANDLE profiled_thread_;
};

#elif V8_OS_FUCHSIA

class Sampler::PlatformData {
 public:
  PlatformData() {
    zx_handle_duplicate(zx_thread_self(), ZX_RIGHT_SAME_RIGHTS,
                        &profiled_thread_);
  }
  ~PlatformData() {
    if (profiled_thread_ != ZX_HANDLE_INVALID) {
      zx_handle_close(profiled_thread_);
      profiled_thread_ = ZX_HANDLE_INVALID;
    }
  }

  zx_handle_t profiled_thread() { return profiled_thread_; }

 private:
  zx_handle_t profiled_thread_ = ZX_HANDLE_INVALID;
};

#endif  // USE_SIGNALS

#if defined(USE_SIGNALS)
class SignalHandler {
 public:
  static void IncreaseSamplerCount() {
    base::RecursiveMutexGuard lock_guard(mutex_.Pointer());
    if (++client_count_ == 1) Install();
  }

  static void DecreaseSamplerCount() {
    base::RecursiveMutexGuard lock_guard(mutex_.Pointer());
    if (--client_count_ == 0) Restore();
  }

  static bool Installed() {
    // mutex_ will also be used in Sampler::DoSample to guard the state below.
    base::RecursiveMutexGuard lock_guard(mutex_.Pointer());
    return signal_handler_installed_;
  }

  static v8::base::RecursiveMutex* mutex() { return mutex_.Pointer(); }

 private:
  static void Install() {
    struct sigaction sa;
    sa.sa_sigaction = &HandleProfilerSignal;
    sigemptyset(&sa.sa_mask);
#if V8_OS_QNX
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
#else
    sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK;
#endif
    signal_handler_installed_ =
        (sigaction(SIGPROF, &sa, &old_signal_handler_) == 0);
  }

  static void Restore() {
    if (signal_handler_installed_) {
      signal_handler_installed_ = false;
#if V8_OS_AIX || V8_TARGET_ARCH_S390X
      // On Aix, IBMi & zLinux SIGPROF can sometimes arrive after the
      // default signal handler is restored, resulting in intermittent test
      // failure when profiling is enabled (https://crbug.com/v8/12952)
      base::OS::Sleep(base::TimeDelta::FromMicroseconds(10));
#endif
      sigaction(SIGPROF, &old_signal_handler_, nullptr);
    }
  }

  static void FillRegisterState(void* context, RegisterState* regs);
  static void HandleProfilerSignal(int signal, siginfo_t* info, void* context);

  // Protects the process wide state below.
  static base::LazyRecursiveMutex mutex_;
  static int client_count_;
  static bool signal_handler_installed_;
  static struct sigaction old_signal_handler_;
};

base::LazyRecursiveMutex SignalHandler::mutex_ =
    LAZY_RECURSIVE_MUTEX_INITIALIZER;

int SignalHandler::client_count_ = 0;
struct sigaction SignalHandler::old_signal_handler_;
bool SignalHandler::signal_handler_installed_ = false;

void SignalHandler::HandleProfilerSignal(int signal, siginfo_t* info,
                                         void* context) {
  v8::ThreadIsolatedAllocator::SetDefaultPermissionsForSignalHandler();
  USE(info);
  if (signal != SIGPROF) return;
  v8::RegisterState state;
  FillRegisterState(context, &state);
  SamplerManager::instance()->DoSample(state);
}

void SignalHandler::FillRegisterState(void* context, RegisterState* state) {
  // Extracting the sample from the context is extremely machine dependent.
  ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
#if !(V8_OS_OPENBSD || \
      (V8_OS_LINUX &&  \
       (V8_HOST_ARCH_PPC || V8_HOST_ARCH_S390 || V8_HOST_ARCH_PPC64)))
  mcontext_t& mcontext = ucontext->uc_mcontext;
#endif
#if V8_OS_LINUX
#if V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(mcontext.gregs[REG_EIP]);
  state->sp = reinterpret_cast<void*>(mcontext.gregs[REG_ESP]);
  state->fp = reinterpret_cast<void*>(mcontext.gregs[REG_EBP]);
#elif V8_HOST_ARCH_X64
  state->pc = reinterpret_cast<void*>(mcontext.gregs[REG_RIP]);
  state->sp = reinterpret_cast<void*>(mcontext.gregs[REG_RSP]);
  state->fp = reinterpret_cast<void*>(mcontext.gregs[REG_RBP]);
#elif V8_HOST_ARCH_ARM
#if V8_LIBC_GLIBC && !V8_GLIBC_PREREQ(2, 4)
  // Old GLibc ARM versions used a gregs[] array to access the register
  // values from mcontext_t.
  state->pc = reinterpret_cast<void*>(mcontext.gregs[R15]);
  state->sp = reinterpret_cast<void*>(mcontext.gregs[R13]);
  state->fp = reinterpret_cast<void*>(mcontext.gregs[R11]);
  state->lr = reinterpret_cast<void*>(mcontext.gregs[R14]);
#else
  state->pc = reinterpret_cast<void*>(mcontext.arm_pc);
  state->sp = reinterpret_cast<void*>(mcontext.arm_sp);
  state->fp = reinterpret_cast<void*>(mcontext.arm_fp);
  state->lr = reinterpret_cast<void*>(mcontext.arm_lr);
#endif  // V8_LIBC_GLIBC && !V8_GLIBC_PREREQ(2, 4)
#elif V8_HOST_ARCH_ARM64
  state->pc = reinterpret_cast<void*>(mcontext.pc);
  state->sp = reinterpret_cast<void*>(mcontext.sp);
  // FP is an alias for x29.
  state->fp = reinterpret_cast<void*>(mcontext.regs[29]);
  // LR is an alias for x30.
  state->lr = reinterpret_cast<void*>(mcontext.regs[30]);
#elif V8_HOST_ARCH_MIPS64
  state->pc = reinterpret_cast<void*>(mcontext.pc);
  state->sp = reinterpret_cast<void*>(mcontext.gregs[29]);
  state->fp = reinterpret_cast<void*>(mcontext.gregs[30]);
#elif V8_HOST_ARCH_LOONG64
  state->pc = reinterpret_cast<void*>(mcontext.__pc);
  state->sp = reinterpret_cast<void*>(mcontext.__gregs[3]);
  state->fp = reinterpret_cast<void*>(mcontext.__gregs[22]);
#elif V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64
#if V8_LIBC_GLIBC
  state->pc = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->nip);
  state->sp = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->gpr[PT_R1]);
  state->fp = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->gpr[PT_R31]);
  state->lr = reinterpret_cast<void*>(ucontext->uc_mcontext.regs->link);
#else
  // Some C libraries, notably Musl, define the regs member as a void pointer
  state->pc = reinterpret_cast<void*>(ucontext->uc_mcontext.gp_regs[32]);
  state->sp = reinterpret_cast<void*>(ucontext->uc_mcontext.gp_regs[1]);
  state->fp = reinterpret_cast<void*>(ucontext->uc_mcontext.gp_regs[31]);
  state->lr = reinterpret_cast<void*>(ucontext->uc_mcontext.gp_regs[36]);
#endif
#elif V8_HOST_ARCH_S390
#if V8_TARGET_ARCH_32_BIT
  // 31-bit target will have bit 0 (MSB) of the PSW set to denote addressing
  // mode.  This bit needs to be masked out to resolve actual address.
  state->pc =
      reinterpret_cast<void*>(ucontext->uc_mcontext.psw.addr & 0x7FFFFFFF);
#else
  state->pc = reinterpret_cast<void*>(ucontext->uc_mcontext.psw.addr);
#endif  // V8_TARGET_ARCH_32_BIT
  state->sp = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[15]);
  state->fp = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[11]);
  state->lr = reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[14]);
#elif V8_HOST_ARCH_RISCV64 || V8_HOST_ARCH_RISCV32
  // Spec CH.25 RISC-V Assembly Programmer’s Handbook
  state->pc = reinterpret_cast<void*>(mcontext.__gregs[REG_PC]);
  state->sp = reinterpret_cast<void*>(mcontext.__gregs[REG_SP]);
  state->fp = reinterpret_cast<void*>(mcontext.__gregs[REG_S0]);
  state->lr = reinterpret_cast<void*>(mcontext.__gregs[REG_RA]);
#endif  // V8_HOST_ARCH_*
#elif V8_OS_IOS

#if V8_TARGET_ARCH_ARM64
  // Building for the iOS device.
  state->pc = reinterpret_cast<void*>(mcontext->__ss.__pc);
  state->sp = reinterpret_cast<void*>(mcontext->__ss.__sp);
  state->fp = reinterpret_cast<void*>(mcontext->__ss.__fp);
#elif V8_TARGET_ARCH_X64
  // Building for the iOS simulator.
  state->pc = reinterpret_cast<void*>(mcontext->__ss.__rip);
  state->sp = reinterpret_cast<void*>(mcontext->__ss.__rsp);
  state->fp = reinterpret_cast<void*>(mcontext->__ss.__rbp);
#else
#error Unexpected iOS target architecture.
#endif  // V8_TARGET_ARCH_ARM64

#elif V8_OS_DARWIN
#if V8_HOST_ARCH_X64
  state->pc = reinterpret_cast<void*>(mcontext->__ss.__rip);
  state->sp = reinterpret_cast<void*>(mcontext->__ss.__rsp);
  state->fp = reinterpret_cast<void*>(mcontext->__ss.__rbp);
#elif V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(mcontext->__ss.__eip);
  state->sp = reinterpret_cast<void*>(mcontext->__ss.__esp);
  state->fp = reinterpret_cast<void*>(mcontext->__ss.__ebp);
#elif V8_HOST_ARCH_ARM64
  state->pc =
      reinterpret_cast<void*>(arm_thread_state64_get_pc(mcontext->__ss));
  state->sp =
      reinterpret_cast<void*>(arm_thread_state64_get_sp(mcontext->__ss));
  state->fp =
      reinterpret_cast<void*>(arm_thread_state64_get_fp(mcontext->__ss));
#endif  // V8_HOST_ARCH_*
#elif V8_OS_FREEBSD
#if V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(mcontext.mc_eip);
  state->sp = reinterpret_cast<void*>(mcontext.mc_esp);
  state->fp = reinterpret_cast<void*>(mcontext.mc_ebp);
#elif V8_HOST_ARCH_X64
  state->pc = reinterpret_cast<void*>(mcontext.mc_rip);
  state->sp = reinterpret_cast<void*>(mcontext.mc_rsp);
  state->fp = reinterpret_cast<void*>(mcontext.mc_rbp);
#elif V8_HOST_ARCH_ARM
  state->pc = reinterpret_cast<void*>(mcontext.__gregs[_REG_PC]);
  state->sp = reinterpret_cast<void*>(mcontext.__gregs[_REG_SP]);
  state->fp = reinterpret_cast<void*>(mcontext.__gregs[_REG_FP]);
#endif  // V8_HOST_ARCH_*
#elif V8_OS_NETBSD
#if V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(mcontext.__gregs[_REG_EIP]);
  state->sp = reinterpret_cast<void*>(mcontext.__gregs[_REG_ESP]);
  state->fp = reinterpret_cast<void*>(mcontext.__gregs[_REG_EBP]);
#elif V8_HOST_ARCH_X64
  state->pc = reinterpret_cast<void*>(mcontext.__gregs[_REG_RIP]);
  state->sp = reinterpret_cast<void*>(mcontext.__gregs[_REG_RSP]);
  state->fp = reinterpret_cast<void*>(mcontext.__gregs[_REG_RBP]);
#endif  // V8_HOST_ARCH_*
#elif V8_OS_OPENBSD
#if V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(ucontext->sc_eip);
  state->sp = reinterpret_cast<void*>(ucontext->sc_esp);
  state->fp = reinterpret_cast<void*>(ucontext->sc_ebp);
#elif V8_HOST_ARCH_X64
  state->pc = reinterpret_cast<void*>(ucontext->sc_rip);
  state->sp = reinterpret_cast<void*>(ucontext->sc_rsp);
  state->fp = reinterpret_cast<void*>(ucontext->sc_rbp);
#endif  // V8_HOST_ARCH_*
#elif V8_OS_SOLARIS
  state->pc = reinterpret_cast<void*>(mcontext.gregs[REG_PC]);
  state->sp = reinterpret_cast<void*>(mcontext.gregs[REG_SP]);
  state->fp = reinterpret_cast<void*>(mcontext.gregs[REG_FP]);
#elif V8_OS_QNX
#if V8_HOST_ARCH_IA32
  state->pc = reinterpret_cast<void*>(mcontext.cpu.eip);
  state->sp = reinterpret_cast<void*>(mcontext.cpu.esp);
  state->fp = reinterpret_cast<void*>(mcontext.cpu.ebp);
#elif V8_HOST_ARCH_ARM
  state->pc = reinterpret_cast<void*>(mcontext.cpu.gpr[ARM_REG_PC]);
  state->sp = reinterpret_cast<void*>(mcontext.cpu.gpr[ARM_REG_SP]);
  state->fp = reinterpret_cast<void*>(mcontext.cpu.gpr[ARM_REG_FP]);
#endif  // V8_HOST_ARCH_*
#elif V8_OS_AIX
  state->pc = reinterpret_cast<void*>(mcontext.jmp_context.iar);
  state->sp = reinterpret_cast<void*>(mcontext.jmp_context.gpr[1]);
  state->fp = reinterpret_cast<void*>(mcontext.jmp_context.gpr[31]);
  state->lr = reinterpret_cast<void*>(mcontext.jmp_context.lr);
#endif  // V8_OS_AIX
}

#endif  // USE_SIGNALS

Sampler::Sampler(Isolate* isolate)
    : isolate_(isolate), data_(std::make_unique<PlatformData>()) {}

Sampler::~Sampler() { DCHECK(!IsActive()); }

void Sampler::Start() {
  DCHECK(!IsActive());
  SetActive(true);
#if defined(USE_SIGNALS)
  SignalHandler::IncreaseSamplerCount();
  SamplerManager::instance()->AddSampler(this);
#endif
}

void Sampler::Stop() {
#if defined(USE_SIGNALS)
  SamplerManager::instance()->RemoveSampler(this);
  SignalHandler::DecreaseSamplerCount();
#endif
  DCHECK(IsActive());
  SetActive(false);
}

#if defined(USE_SIGNALS)

void Sampler::DoSample() {
  base::RecursiveMutexGuard lock_guard(SignalHandler::mutex());
  if (!SignalHandler::Installed()) return;
  SetShouldRecordSample();
  pthread_kill(platform_data()->vm_tid(), SIGPROF);
}

#elif V8_OS_WIN || V8_OS_CYGWIN

void Sampler::DoSample() {
  HANDLE profiled_thread = platform_data()->profiled_thread();
  if (profiled_thread == nullptr) return;

  const DWORD kSuspendFailed = static_cast<DWORD>(-1);
  if (SuspendThread(profiled_thread) == kSuspendFailed) return;

  // Context used for sampling the register state of the profiled thread.
  CONTEXT context;
  memset(&context, 0, sizeof(context));
  context.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(profiled_thread, &context) != 0) {
    v8::RegisterState state;
#if V8_HOST_ARCH_X64
    state.pc = reinterpret_cast<void*>(context.Rip);
    state.sp = reinterpret_cast<void*>(context.Rsp);
    state.fp = reinterpret_cast<void*>(context.Rbp);
#elif V8_HOST_ARCH_ARM64
    state.pc = reinterpret_cast<void*>(context.Pc);
    state.sp = reinterpret_cast<void*>(context.Sp);
    state.fp = reinterpret_cast<void*>(context.Fp);
#else
    state.pc = reinterpret_cast<void*>(context.Eip);
    state.sp = reinterpret_cast<void*>(context.Esp);
    state.fp = reinterpret_cast<void*>(context.Ebp);
#endif
    SampleStack(state);
  }
  ResumeThread(profiled_thread);
}

#elif V8_OS_FUCHSIA

void Sampler::DoSample() {
  zx_handle_t profiled_thread = platform_data()->profiled_thread();
  if (profiled_thread == ZX_HANDLE_INVALID) return;

  zx_handle_t suspend_token = ZX_HANDLE_INVALID;
  if (zx_task_suspend_token(profiled_thread, &suspend_token) != ZX_OK) return;

  // Wait for the target thread to become suspended, or to exit.
  // TODO(wez): There is currently no suspension count for threads, so there
  // is a risk that some other caller resumes the thread in-between our suspend
  // and wait calls, causing us to miss the SUSPENDED signal. We apply a 100ms
  // deadline to protect against hanging the sampler thread in this case.
  zx_signals_t signals = 0;
  zx_status_t suspended = zx_object_wait_one(
      profiled_thread, ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED,
      zx_deadline_after(ZX_MSEC(100)), &signals);
  if (suspended != ZX_OK || (signals & ZX_THREAD_SUSPENDED) == 0) {
    zx_handle_close(suspend_token);
    return;
  }

  // Fetch a copy of its "general register" states.
  zx_thread_state_general_regs_t thread_state = {};
  if (zx_thread_read_state(profiled_thread, ZX_THREAD_STATE_GENERAL_REGS,
                           &thread_state, sizeof(thread_state)) == ZX_OK) {
    v8::RegisterState state;
#if V8_HOST_ARCH_X64
    state.pc = reinterpret_cast<void*>(thread_state.rip);
    state.sp = reinterpret_cast<void*>(thread_state.rsp);
    state.fp = reinterpret_cast<void*>(thread_state.rbp);
#elif V8_HOST_ARCH_ARM64
    state.pc = reinterpret_cast<void*>(thread_state.pc);
    state.sp = reinterpret_cast<void*>(thread_state.sp);
    state.fp = reinterpret_cast<void*>(thread_state.r[29]);
#endif
    SampleStack(state);
  }

  zx_handle_close(suspend_token);
}

// TODO(wez): Remove this once the Fuchsia SDK has rolled.
#if defined(ZX_THREAD_STATE_REGSET0)
#undef ZX_THREAD_STATE_GENERAL_REGS
#endif

#endif  // USE_SIGNALS

}  // namespace sampler
}  // namespace v8
