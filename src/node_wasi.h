#ifndef SRC_NODE_WASI_H_
#define SRC_NODE_WASI_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "base_object.h"
#include "node_mem.h"
#include "uvwasi.h"
#include "v8-fast-api-calls.h"

namespace node {
namespace wasi {

struct WasmMemory {
  char* data;
  size_t size;
};

class WASI : public BaseObject,
             public mem::NgLibMemoryManager<WASI, uvwasi_mem_t> {
 public:
  WASI(Environment* env,
       v8::Local<v8::Object> object,
       uvwasi_options_t* options);
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(WASI)
  SET_SELF_SIZE(WASI)

  static uint32_t ArgsGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t ArgsSizesGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t ClockResGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t ClockTimeGet(WASI&, WasmMemory, uint32_t, uint64_t, uint32_t);
  static uint32_t EnvironGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t EnvironSizesGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdAdvise(
      WASI&, WasmMemory, uint32_t, uint64_t, uint64_t, uint32_t);
  static uint32_t FdAllocate(WASI&, WasmMemory, uint32_t, uint64_t, uint64_t);
  static uint32_t FdClose(WASI&, WasmMemory, uint32_t);
  static uint32_t FdDatasync(WASI&, WasmMemory, uint32_t);
  static uint32_t FdFdstatGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdFdstatSetFlags(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdFdstatSetRights(
      WASI&, WasmMemory, uint32_t, uint64_t, uint64_t);
  static uint32_t FdFilestatGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdFilestatSetSize(WASI&, WasmMemory, uint32_t, uint64_t);
  static uint32_t FdFilestatSetTimes(
      WASI&, WasmMemory, uint32_t, uint64_t, uint64_t, uint32_t);
  static uint32_t FdPread(WASI&,
                          WasmMemory memory,
                          uint32_t,
                          uint32_t,
                          uint32_t,
                          uint64_t,
                          uint32_t);
  static uint32_t FdPrestatGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdPrestatDirName(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t);
  static uint32_t FdPwrite(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t);
  static uint32_t FdRead(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t);
  static uint32_t FdReaddir(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t);
  static uint32_t FdRenumber(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdSeek(
      WASI&, WasmMemory, uint32_t, int64_t, uint32_t, uint32_t);
  static uint32_t FdSync(WASI&, WasmMemory, uint32_t);
  static uint32_t FdTell(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t FdWrite(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t);
  static uint32_t PathCreateDirectory(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t);
  static uint32_t PathFilestatGet(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  static uint32_t PathFilestatSetTimes(WASI&,
                                       WasmMemory,
                                       uint32_t,
                                       uint32_t,
                                       uint32_t,
                                       uint32_t,
                                       uint64_t,
                                       uint64_t,
                                       uint32_t);
  static uint32_t PathLink(WASI&,
                           WasmMemory,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t);
  static uint32_t PathOpen(WASI&,
                           WasmMemory,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint64_t,
                           uint64_t,
                           uint32_t,
                           uint32_t);
  static uint32_t PathReadlink(WASI&,
                               WasmMemory,
                               uint32_t,
                               uint32_t,
                               uint32_t,
                               uint32_t,
                               uint32_t,
                               uint32_t);
  static uint32_t PathRemoveDirectory(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t);
  static uint32_t PathRename(WASI&,
                             WasmMemory,
                             uint32_t,
                             uint32_t,
                             uint32_t,
                             uint32_t,
                             uint32_t,
                             uint32_t);
  static uint32_t PathSymlink(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  static uint32_t PathUnlinkFile(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t);
  static uint32_t PollOneoff(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t);
  static void ProcExit(WASI&, WasmMemory, uint32_t);
  static uint32_t ProcRaise(WASI&, WasmMemory, uint32_t);
  static uint32_t RandomGet(WASI&, WasmMemory, uint32_t, uint32_t);
  static uint32_t SchedYield(WASI&, WasmMemory);
  static uint32_t SockAccept(WASI&, WasmMemory, uint32_t, uint32_t, uint32_t);
  static uint32_t SockRecv(WASI&,
                           WasmMemory,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t,
                           uint32_t);
  static uint32_t SockSend(
      WASI&, WasmMemory, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  static uint32_t SockShutdown(WASI&, WasmMemory, uint32_t, uint32_t);

  static void _SetMemory(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Implementation for mem::NgLibMemoryManager
  void CheckAllocatedSize(size_t previous_size) const;
  void IncreaseAllocatedSize(size_t size);
  void DecreaseAllocatedSize(size_t size);

  // <typename FT, FT F> as a C++14 desugaring of `<auto F>`
  template <typename FT, FT F, typename R, typename... Args>
  class WasiFunction {
   public:
    static void SetFunction(Environment*,
                            const char*,
                            v8::Local<v8::FunctionTemplate>);

   private:
    static R FastCallback(v8::Local<v8::Object> unused,
                          v8::Local<v8::Object> receiver,
                          Args...,
                          v8::FastApiCallbackOptions&);

    static void SlowCallback(const v8::FunctionCallbackInfo<v8::Value>&);
  };

 private:
  ~WASI() override;
  uvwasi_t uvw_;
  v8::Global<v8::WasmMemoryObject> memory_;
  uvwasi_mem_t alloc_info_;
  size_t current_uvwasi_memory_ = 0;
};


}  // namespace wasi
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_WASI_H_
