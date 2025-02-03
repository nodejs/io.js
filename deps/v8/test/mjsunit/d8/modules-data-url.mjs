// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { a, b } from "data:text/javascript,export let a = 'b'; export let b = 'a';";
assertEquals('b', a);
assertEquals('a', b);

import x from "data:text/javascript,export default 7";
assertEquals(7, x);

import * as y from "data:text/javascript,export function q() { return 42; }";
assertEquals(42, y.q());

let z = await import("data:text/javascript,export const z = 5");
assertEquals(5, z.z);
