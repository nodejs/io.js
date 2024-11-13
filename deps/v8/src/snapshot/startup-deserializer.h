// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_STARTUP_DESERIALIZER_H_
#define V8_SNAPSHOT_STARTUP_DESERIALIZER_H_

#include "src/snapshot/deserializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

// Initializes an isolate with context-independent data from a given snapshot.
class StartupDeserializer final : public Deserializer<Isolate> {
 public:
  explicit StartupDeserializer(Isolate* isolate,
                               const SnapshotData* startup_data,
                               bool can_rehash)
      : Deserializer(isolate, startup_data->Payload(),
                     startup_data->GetMagicNumber(), false, can_rehash) {}

  // Deserialize the snapshot into an empty heap.
  void DeserializeIntoIsolate();

 private:
  void FlushICache();
  void LogNewMapEvents();
  void DeserializeAndCheckExternalReferenceTable();
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_STARTUP_DESERIALIZER_H_
