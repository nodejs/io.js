// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-temporal

// For now, just make sure to call this function at least once in our tests.
// TODO(temporal): Functional tests.
new Date().toTemporalInstant();
