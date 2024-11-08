/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>

#include "uv.h"
#include "internal.h"


/* Ntdll function pointers */
sRtlGetVersion pRtlGetVersion;
sRtlNtStatusToDosError pRtlNtStatusToDosError;
sNtDeviceIoControlFile pNtDeviceIoControlFile;
sNtQueryInformationFile pNtQueryInformationFile;
sNtSetInformationFile pNtSetInformationFile;
sNtQueryVolumeInformationFile pNtQueryVolumeInformationFile;
sNtQueryDirectoryFile pNtQueryDirectoryFile;
sNtQuerySystemInformation pNtQuerySystemInformation;
sNtQueryInformationProcess pNtQueryInformationProcess;

/* Kernel32 function pointers */
sGetQueuedCompletionStatusEx pGetQueuedCompletionStatusEx;

/* Powrprof.dll function pointer */
sPowerRegisterSuspendResumeNotification pPowerRegisterSuspendResumeNotification;

/* User32.dll function pointer */
sSetWinEventHook pSetWinEventHook;

/* ws2_32.dll function pointer */
uv_sGetHostNameW pGetHostNameW;

/* api-ms-win-core-file-l2-1-4.dll function pointer */
sGetFileInformationByName pGetFileInformationByName;

void uv__winapi_init(void) {
  HMODULE ntdll_module;
  HMODULE powrprof_module;
  HMODULE user32_module;
  HMODULE kernel32_module;
  HMODULE ws2_32_module;
  HMODULE api_win_core_file_module;

  ntdll_module = GetModuleHandleA("ntdll.dll");
  if (ntdll_module == NULL) {
    uv_fatal_error(GetLastError(), "GetModuleHandleA");
  }

  pRtlGetVersion = (sRtlGetVersion) GetProcAddress(ntdll_module,
                                                   "RtlGetVersion");

  pRtlNtStatusToDosError = (sRtlNtStatusToDosError) GetProcAddress(
      ntdll_module,
      "RtlNtStatusToDosError");
  if (pRtlNtStatusToDosError == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtDeviceIoControlFile = (sNtDeviceIoControlFile) GetProcAddress(
      ntdll_module,
      "NtDeviceIoControlFile");
  if (pNtDeviceIoControlFile == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtQueryInformationFile = (sNtQueryInformationFile) GetProcAddress(
      ntdll_module,
      "NtQueryInformationFile");
  if (pNtQueryInformationFile == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtSetInformationFile = (sNtSetInformationFile) GetProcAddress(
      ntdll_module,
      "NtSetInformationFile");
  if (pNtSetInformationFile == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtQueryVolumeInformationFile = (sNtQueryVolumeInformationFile)
      GetProcAddress(ntdll_module, "NtQueryVolumeInformationFile");
  if (pNtQueryVolumeInformationFile == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtQueryDirectoryFile = (sNtQueryDirectoryFile)
      GetProcAddress(ntdll_module, "NtQueryDirectoryFile");
  if (pNtQueryDirectoryFile == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtQuerySystemInformation = (sNtQuerySystemInformation) GetProcAddress(
      ntdll_module,
      "NtQuerySystemInformation");
  if (pNtQuerySystemInformation == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  pNtQueryInformationProcess = (sNtQueryInformationProcess) GetProcAddress(
      ntdll_module,
      "NtQueryInformationProcess");
  if (pNtQueryInformationProcess == NULL) {
    uv_fatal_error(GetLastError(), "GetProcAddress");
  }

  kernel32_module = GetModuleHandleA("kernel32.dll");
  if (kernel32_module == NULL) {
    uv_fatal_error(GetLastError(), "GetModuleHandleA");
  }

  pGetQueuedCompletionStatusEx = (sGetQueuedCompletionStatusEx) GetProcAddress(
      kernel32_module,
      "GetQueuedCompletionStatusEx");

  powrprof_module = LoadLibraryExA("powrprof.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (powrprof_module != NULL) {
    pPowerRegisterSuspendResumeNotification = (sPowerRegisterSuspendResumeNotification)
      GetProcAddress(powrprof_module, "PowerRegisterSuspendResumeNotification");
  }

  user32_module = GetModuleHandleA("user32.dll");
  if (user32_module != NULL) {
    pSetWinEventHook = (sSetWinEventHook)
      GetProcAddress(user32_module, "SetWinEventHook");
  }

  ws2_32_module = GetModuleHandleA("ws2_32.dll");
  if (ws2_32_module != NULL) {
    pGetHostNameW = (uv_sGetHostNameW) GetProcAddress(
        ws2_32_module,
        "GetHostNameW");
  }

  api_win_core_file_module = GetModuleHandleA("api-ms-win-core-file-l2-1-4.dll");
  if (api_win_core_file_module != NULL) {
    pGetFileInformationByName = (sGetFileInformationByName)GetProcAddress(
        api_win_core_file_module, "GetFileInformationByName");
  }
}
