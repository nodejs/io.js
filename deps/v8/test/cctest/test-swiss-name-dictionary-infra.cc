// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-swiss-name-dictionary-infra.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

namespace {
std::vector<PropertyDetails> MakeDistinctDetails() {
  std::vector<PropertyDetails> result(32, PropertyDetails::Empty());

  int i = 0;
  for (PropertyKind kind : {PropertyKind::kAccessor, PropertyKind::kAccessor}) {
    for (PropertyConstness constness :
         {PropertyConstness::kConst, PropertyConstness::kMutable}) {
      for (bool writeable : {true, false}) {
        for (bool enumerable : {true, false}) {
          for (bool configurable : {true, false}) {
            uint8_t attrs = static_cast<uint8_t>(PropertyAttributes::NONE);
            if (!writeable) attrs |= PropertyAttributes::READ_ONLY;
            if (!enumerable) {
              attrs |= PropertyAttributes::DONT_ENUM;
            }
            if (!configurable) {
              attrs |= PropertyAttributes::DONT_DELETE;
            }
            auto attributes = PropertyAttributesFromInt(attrs);
            PropertyDetails details(kind, attributes,
                                    PropertyCellType::kNoCell);
            details = details.CopyWithConstness(constness);
            result[i++] = details;
          }
        }
      }
    }
  }
  return result;
}

}  // namespace

// To enable more specific testing, we allow overriding the H1 and H2 hashes for
// a key before adding it to the SwissNameDictionary. The necessary overriding
// of the stored hash happens here. Symbols are compared by identity, we cache
// the Symbol associated with each std::string key. This means that using
// "my_key" twice in the same TestSequence will return the same Symbol
// associated with "my_key" both times. This also means that within a given
// TestSequence, we cannot use the same (std::string) key with different faked
// hashes.
DirectHandle<Name> CreateKeyWithHash(Isolate* isolate, KeyCache& keys,
                                     const Key& key) {
  Handle<Symbol> key_symbol;
  auto iter = keys.find(key.str);

  if (iter == keys.end()) {
    // We haven't seen the the given string as a key in the current
    // TestSequence. Create it, fake its hash if requested and cache it.

    key_symbol = isolate->factory()->NewSymbol();

    // We use the description field to store the original string key for
    // debugging.
    DirectHandle<String> description =
        isolate->factory()->NewStringFromAsciiChecked(key.str.c_str());
    key_symbol->set_description(*description);

    CachedKey new_info = {key_symbol, key.h1_override, key.h2_override};
    keys[key.str] = new_info;

    if (key.h1_override || key.h2_override) {
      uint32_t actual_hash = key_symbol->hash();
      int fake_hash = actual_hash;
      if (key.h1_override) {
        uint32_t override_with = key.h1_override.value().value;

        // We cannot override h1 with 0 unless we also override h2 with a
        // non-zero value. Otherwise, the overall hash may become 0 (which is
        // forbidden) based on the (nondeterministic) choice of h2.
        CHECK_IMPLIES(override_with == 0,
                      key.h2_override && key.h2_override.value().value != 0);

        fake_hash = (override_with << swiss_table::kH2Bits) |
                    swiss_table::H2(actual_hash);
      }
      if (key.h2_override) {
        // Unset  7 bits belonging to H2:
        fake_hash &= ~((1 << swiss_table::kH2Bits) - 1);

        uint8_t override_with = key.h2_override.value().value;

        // Same as above, but for h2: Prevent accidentally creating 0 fake hash.
        CHECK_IMPLIES(override_with == 0,
                      key.h1_override && key.h1_override.value().value != 0);

        CHECK_LT(key.h2_override.value().value, 1 << swiss_table::kH2Bits);
        fake_hash |= swiss_table::H2(override_with);
      }

      // Prepare what to put into the hash field.
      uint32_t hash_field =
          Name::CreateHashFieldValue(fake_hash, Name::HashFieldType::kHash);
      CHECK_NE(hash_field, 0);

      key_symbol->set_raw_hash_field(hash_field);
      CHECK_EQ(fake_hash, key_symbol->hash());
    }

    return key_symbol;
  } else {
    // We've seen this key before. Return the cached version.
    CachedKey& cached_info = iter->second;

    // Internal consistency check: Make sure that we didn't request something
    // else w.r.t. hash faking when using this key before. If so, the test case
    // would make inconsistent assumptions about how the hashes should be faked
    // and be broken.
    CHECK_EQ(cached_info.h1_override, key.h1_override);
    CHECK_EQ(cached_info.h2_override, key.h2_override);

    return cached_info.key_symbol;
  }
}

const std::vector<PropertyDetails> distinct_property_details =
    MakeDistinctDetails();

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
