// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#define FIELD(op, field_name)                                       \
  OpMaskField<UnwrapRepresentation<decltype(op::field_name)>::type, \
              OFFSET_OF(op, field_name)>
