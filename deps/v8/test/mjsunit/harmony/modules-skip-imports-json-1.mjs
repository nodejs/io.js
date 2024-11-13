// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-import-assertions

import json from "modules-skip-1.json" assert { type: "json" };
export function life() { return json.life; }
