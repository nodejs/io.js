// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
const err = new Error(undefined);
delete err.stack;
err.cause = err;

const err_serialized = d8.serializer.serialize(err);
d8.serializer.deserialize(err_serialized);
