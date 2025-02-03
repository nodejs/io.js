// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   https://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFTimeZone.h>

#include <vector>
#endif

#if defined(__Fuchsia__)
#include <fuchsia/intl/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <zircon/types.h>
#endif

#if defined(_WIN32)
#include <sdkddkver.h>
// Include only when the SDK is for Windows 10 (and later), and the binary is
// targeted for Windows XP and later.
// Note: The Windows SDK added windows.globalization.h file for Windows 10, but
// MinGW did not add it until NTDDI_WIN10_NI (SDK version 10.0.22621.0).
#if ((defined(_WIN32_WINNT_WIN10) && !defined(__MINGW32__)) ||        \
     (defined(NTDDI_WIN10_NI) && NTDDI_VERSION >= NTDDI_WIN10_NI)) && \
    (_WIN32_WINNT >= _WIN32_WINNT_WINXP)
#define USE_WIN32_LOCAL_TIME_ZONE
#include <roapi.h>
#include <tchar.h>
#include <wchar.h>
#include <windows.globalization.h>
#include <windows.h>
#endif
#endif

#include <cstdlib>
#include <cstring>
#include <string>

#include "time_zone_fixed.h"
#include "time_zone_impl.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

namespace {
#if defined(USE_WIN32_LOCAL_TIME_ZONE)
// Calls the WinRT Calendar.GetTimeZone method to obtain the IANA ID of the
// local time zone. Returns an empty vector in case of an error.
std::string win32_local_time_zone(const HMODULE combase) {
  std::string result;
  const auto ro_activate_instance =
      reinterpret_cast<decltype(&RoActivateInstance)>(
          GetProcAddress(combase, "RoActivateInstance"));
  if (!ro_activate_instance) {
    return result;
  }
  const auto windows_create_string_reference =
      reinterpret_cast<decltype(&WindowsCreateStringReference)>(
          GetProcAddress(combase, "WindowsCreateStringReference"));
  if (!windows_create_string_reference) {
    return result;
  }
  const auto windows_delete_string =
      reinterpret_cast<decltype(&WindowsDeleteString)>(
          GetProcAddress(combase, "WindowsDeleteString"));
  if (!windows_delete_string) {
    return result;
  }
  const auto windows_get_string_raw_buffer =
      reinterpret_cast<decltype(&WindowsGetStringRawBuffer)>(
          GetProcAddress(combase, "WindowsGetStringRawBuffer"));
  if (!windows_get_string_raw_buffer) {
    return result;
  }

  // The string returned by WindowsCreateStringReference doesn't need to be
  // deleted.
  HSTRING calendar_class_id;
  HSTRING_HEADER calendar_class_id_header;
  HRESULT hr = windows_create_string_reference(
      RuntimeClass_Windows_Globalization_Calendar,
      sizeof(RuntimeClass_Windows_Globalization_Calendar) / sizeof(wchar_t) - 1,
      &calendar_class_id_header, &calendar_class_id);
  if (FAILED(hr)) {
    return result;
  }

  IInspectable* calendar;
  hr = ro_activate_instance(calendar_class_id, &calendar);
  if (FAILED(hr)) {
    return result;
  }

  ABI::Windows::Globalization::ITimeZoneOnCalendar* time_zone;
  hr = calendar->QueryInterface(IID_PPV_ARGS(&time_zone));
  if (FAILED(hr)) {
    calendar->Release();
    return result;
  }

  HSTRING tz_hstr;
  hr = time_zone->GetTimeZone(&tz_hstr);
  if (SUCCEEDED(hr)) {
    UINT32 wlen;
    const PCWSTR tz_wstr = windows_get_string_raw_buffer(tz_hstr, &wlen);
    if (tz_wstr) {
      const int size =
          WideCharToMultiByte(CP_UTF8, 0, tz_wstr, static_cast<int>(wlen),
                              nullptr, 0, nullptr, nullptr);
      result.resize(static_cast<size_t>(size));
      WideCharToMultiByte(CP_UTF8, 0, tz_wstr, static_cast<int>(wlen),
                          &result[0], size, nullptr, nullptr);
    }
    windows_delete_string(tz_hstr);
  }
  time_zone->Release();
  calendar->Release();
  return result;
}
#endif
}  // namespace

std::string time_zone::name() const { return effective_impl().Name(); }

time_zone::absolute_lookup time_zone::lookup(
    const time_point<seconds>& tp) const {
  return effective_impl().BreakTime(tp);
}

time_zone::civil_lookup time_zone::lookup(const civil_second& cs) const {
  return effective_impl().MakeTime(cs);
}

bool time_zone::next_transition(const time_point<seconds>& tp,
                                civil_transition* trans) const {
  return effective_impl().NextTransition(tp, trans);
}

bool time_zone::prev_transition(const time_point<seconds>& tp,
                                civil_transition* trans) const {
  return effective_impl().PrevTransition(tp, trans);
}

std::string time_zone::version() const { return effective_impl().Version(); }

std::string time_zone::description() const {
  return effective_impl().Description();
}

const time_zone::Impl& time_zone::effective_impl() const {
  if (impl_ == nullptr) {
    // Dereferencing an implicit-UTC time_zone is expected to be
    // rare, so we don't mind paying a small synchronization cost.
    return *time_zone::Impl::UTC().impl_;
  }
  return *impl_;
}

bool load_time_zone(const std::string& name, time_zone* tz) {
  return time_zone::Impl::LoadTimeZone(name, tz);
}

time_zone utc_time_zone() {
  return time_zone::Impl::UTC();  // avoid name lookup
}

time_zone fixed_time_zone(const seconds& offset) {
  time_zone tz;
  load_time_zone(FixedOffsetToName(offset), &tz);
  return tz;
}

time_zone local_time_zone() {
  const char* zone = ":localtime";
#if defined(__ANDROID__)
  char sysprop[PROP_VALUE_MAX];
  if (__system_property_get("persist.sys.timezone", sysprop) > 0) {
    zone = sysprop;
  }
#endif
#if defined(__APPLE__)
  std::vector<char> buffer;
  CFTimeZoneRef tz_default = CFTimeZoneCopyDefault();
  if (CFStringRef tz_name = CFTimeZoneGetName(tz_default)) {
    CFStringEncoding encoding = kCFStringEncodingUTF8;
    CFIndex length = CFStringGetLength(tz_name);
    CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, encoding) + 1;
    buffer.resize(static_cast<size_t>(max_size));
    if (CFStringGetCString(tz_name, &buffer[0], max_size, encoding)) {
      zone = &buffer[0];
    }
  }
  CFRelease(tz_default);
#endif
#if defined(__Fuchsia__)
  std::string primary_tz;
  [&]() {
    // Note: We can't use the synchronous FIDL API here because it doesn't
    // allow timeouts; if the FIDL call failed, local_time_zone() would never
    // return.

    const zx::duration kTimeout = zx::msec(500);

    // Don't attach to the thread because otherwise the thread's dispatcher
    // would be set to null when the loop is destroyed, causing any other FIDL
    // code running on the same thread to crash.
    async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

    fuchsia::intl::PropertyProviderHandle handle;
    zx_status_t status = fdio_service_connect_by_name(
        fuchsia::intl::PropertyProvider::Name_,
        handle.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      return;
    }

    fuchsia::intl::PropertyProviderPtr intl_provider;
    status = intl_provider.Bind(std::move(handle), loop.dispatcher());
    if (status != ZX_OK) {
      return;
    }

    intl_provider->GetProfile(
        [&loop, &primary_tz](fuchsia::intl::Profile profile) {
          if (!profile.time_zones().empty()) {
            primary_tz = profile.time_zones()[0].id;
          }
          loop.Quit();
        });
    loop.Run(zx::deadline_after(kTimeout));
  }();

  if (!primary_tz.empty()) {
    zone = primary_tz.c_str();
  }
#endif
#if defined(USE_WIN32_LOCAL_TIME_ZONE)
  // Use the WinRT Calendar class to get the local time zone. This feature is
  // available on Windows 10 and later. The library is dynamically linked to
  // maintain binary compatibility with Windows XP - Windows 7. On Windows 8,
  // The combase.dll API functions are available but the RoActivateInstance
  // call will fail for the Calendar class.
  std::string winrt_tz;
  const HMODULE combase =
      LoadLibraryEx(_T("combase.dll"), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (combase) {
    const auto ro_initialize = reinterpret_cast<decltype(&::RoInitialize)>(
        GetProcAddress(combase, "RoInitialize"));
    const auto ro_uninitialize = reinterpret_cast<decltype(&::RoUninitialize)>(
        GetProcAddress(combase, "RoUninitialize"));
    if (ro_initialize && ro_uninitialize) {
      const HRESULT hr = ro_initialize(RO_INIT_MULTITHREADED);
      // RPC_E_CHANGED_MODE means that a previous RoInitialize call specified
      // a different concurrency model. The WinRT runtime is initialized and
      // should work for our purpose here, but we should *not* call
      // RoUninitialize because it's a failure.
      if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        winrt_tz = win32_local_time_zone(combase);
        if (SUCCEEDED(hr)) {
          ro_uninitialize();
        }
      }
    }
    FreeLibrary(combase);
  }
  if (!winrt_tz.empty()) {
    zone = winrt_tz.c_str();
  }
#endif

  // Allow ${TZ} to override to default zone.
  char* tz_env = nullptr;
#if defined(_MSC_VER)
  _dupenv_s(&tz_env, nullptr, "TZ");
#else
  tz_env = std::getenv("TZ");
#endif
  if (tz_env) zone = tz_env;

  // We only support the "[:]<zone-name>" form.
  if (*zone == ':') ++zone;

  // Map "localtime" to a system-specific name, but
  // allow ${LOCALTIME} to override the default name.
  char* localtime_env = nullptr;
  if (strcmp(zone, "localtime") == 0) {
#if defined(_MSC_VER)
    // System-specific default is just "localtime".
    _dupenv_s(&localtime_env, nullptr, "LOCALTIME");
#else
    zone = "/etc/localtime";  // System-specific default.
    localtime_env = std::getenv("LOCALTIME");
#endif
    if (localtime_env) zone = localtime_env;
  }

  const std::string name = zone;
#if defined(_MSC_VER)
  free(localtime_env);
  free(tz_env);
#endif

  time_zone tz;
  load_time_zone(name, &tz);  // Falls back to UTC.
  // TODO: Follow the RFC3339 "Unknown Local Offset Convention" and
  // arrange for %z to generate "-0000" when we don't know the local
  // offset because the load_time_zone() failed and we're using UTC.
  return tz;
}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
