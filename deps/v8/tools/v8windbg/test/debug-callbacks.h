// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TOOLS_V8WINDBG_TEST_DEBUG_CALLBACKS_H_
#define V8_TOOLS_V8WINDBG_TEST_DEBUG_CALLBACKS_H_

#if !defined(UNICODE) || !defined(_UNICODE)
#error Unicode not defined
#endif

// Must be included before DbgModel.h.
#include <new>

#include <DbgEng.h>
#include <DbgModel.h>
#include <Windows.h>
#include <crtdbg.h>
#include <pathcch.h>
#include <wrl/client.h>

#include <string>

namespace WRL = Microsoft::WRL;

namespace v8 {
namespace internal {
namespace v8windbg_test {

class MyOutput : public IDebugOutputCallbacks {
 public:
  MyOutput(WRL::ComPtr<IDebugClient5> p_client);
  ~MyOutput();
  MyOutput(const MyOutput&) = delete;
  MyOutput& operator=(const MyOutput&) = delete;

  // Inherited via IDebugOutputCallbacks
  HRESULT __stdcall QueryInterface(REFIID InterfaceId,
                                   PVOID* Interface) override;
  ULONG __stdcall AddRef(void) override;
  ULONG __stdcall Release(void) override;
  HRESULT __stdcall Output(ULONG Mask, PCSTR Text) override;

  const std::string& GetLog() const { return log_; }
  void ClearLog() { log_.clear(); }

 private:
  WRL::ComPtr<IDebugClient5> p_client_;
  std::string log_;
};

// For return values, see:
// https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/debug-status-xxx
class MyCallback : public IDebugEventCallbacks {
 public:
  // Inherited via IDebugEventCallbacks
  HRESULT __stdcall QueryInterface(REFIID InterfaceId,
                                   PVOID* Interface) override;
  ULONG __stdcall AddRef(void) override;
  ULONG __stdcall Release(void) override;
  HRESULT __stdcall GetInterestMask(PULONG Mask) override;
  HRESULT __stdcall Breakpoint(PDEBUG_BREAKPOINT Bp) override;
  HRESULT __stdcall Exception(PEXCEPTION_RECORD64 Exception,
                              ULONG FirstChance) override;
  HRESULT __stdcall CreateThread(ULONG64 Handle, ULONG64 DataOffset,
                                 ULONG64 StartOffset) override;
  HRESULT __stdcall ExitThread(ULONG ExitCode) override;
  HRESULT __stdcall ExitProcess(ULONG ExitCode) override;
  HRESULT __stdcall LoadModule(ULONG64 ImageFileHandle, ULONG64 BaseOffset,
                               ULONG ModuleSize, PCSTR ModuleName,
                               PCSTR ImageName, ULONG CheckSum,
                               ULONG TimeDateStamp) override;
  HRESULT __stdcall UnloadModule(PCSTR ImageBaseName,
                                 ULONG64 BaseOffset) override;
  HRESULT __stdcall SystemError(ULONG Error, ULONG Level) override;
  HRESULT __stdcall SessionStatus(ULONG Status) override;
  HRESULT __stdcall ChangeDebuggeeState(ULONG Flags, ULONG64 Argument) override;
  HRESULT __stdcall ChangeEngineState(ULONG Flags, ULONG64 Argument) override;
  HRESULT __stdcall ChangeSymbolState(ULONG Flags, ULONG64 Argument) override;
  HRESULT __stdcall CreateProcessW(ULONG64 ImageFileHandle, ULONG64 Handle,
                                   ULONG64 BaseOffset, ULONG ModuleSize,
                                   PCSTR ModuleName, PCSTR ImageName,
                                   ULONG CheckSum, ULONG TimeDateStamp,
                                   ULONG64 InitialThreadHandle,
                                   ULONG64 ThreadDataOffset,
                                   ULONG64 StartOffset) override;
};

}  // namespace v8windbg_test
}  // namespace internal
}  // namespace v8

#endif  // V8_TOOLS_V8WINDBG_TEST_DEBUG_CALLBACKS_H_
