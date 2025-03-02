// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export * as self from "./modules-export-star-as2.mjs";
export * as self_again from "./modules-export-star-as2.mjs";
import {self as myself} from "./modules-export-star-as2.mjs";
import {self_again as myself_again} from "./modules-export-star-as2.mjs";

assertEquals(["self", "self_again"], Object.keys(myself));
assertEquals(myself, myself.self);
assertEquals(myself_again, myself.self_again);
assertEquals(myself, myself_again);

assertThrows(_ => self, ReferenceError);
assertThrows(_ => self_again, ReferenceError);
