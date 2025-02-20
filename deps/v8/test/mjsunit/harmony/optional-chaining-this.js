// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


const o = { m() { return this; }, p: 42 };
const p = { o: o };

function id(x) { return x; }

assertEquals(o?.m().p, 42);
assertEquals((o?.m)().p, 42);
assertEquals(o?.m(), (o?.m)());
assertEquals(p?.o?.m().p, 42);
assertEquals((p?.o?.m)().p, 42);
assertEquals(p?.o?.m(), (p?.o?.m)());

assertEquals(o?.[id('m')]().p, 42);
assertEquals((o?.[id('m')])().p, 42);
assertEquals(o?.[id('m')](), (o?.[id('m')])());
assertEquals(p?.[id('o')]?.[id('m')]().p, 42);
assertEquals((p?.[id('o')]?.[id('m')])().p, 42);
assertEquals(p?.[id('o')]?.[id('m')](), (p?.[id('o')]?.[id('m')])());
