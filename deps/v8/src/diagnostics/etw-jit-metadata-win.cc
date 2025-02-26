// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-jit-metadata-win.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

void SetMetaDescriptors(EVENT_DATA_DESCRIPTOR* data_descriptor,
                        UINT16 const UNALIGNED* traits, const void* metadata,
                        size_t size) {
  // The first descriptor is the provider traits (just the name currently)
  uint16_t traits_size = *reinterpret_cast<const uint16_t*>(traits);
  EventDataDescCreate(data_descriptor, traits, traits_size);
  data_descriptor->Type = EVENT_DATA_DESCRIPTOR_TYPE_PROVIDER_METADATA;
  ++data_descriptor;

  // The second descriptor contains the data to describe the field layout
  EventDataDescCreate(data_descriptor, metadata, static_cast<ULONG>(size));
  data_descriptor->Type = EVENT_DATA_DESCRIPTOR_TYPE_EVENT_METADATA;
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
