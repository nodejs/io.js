// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --throws

d8.debugger.enable();
eval(`import('I-do-not-exist.js');`);
// Cause a pending termination that needs to be handled
// in DoHostImportModuleDynamically.
d8.terminate();
