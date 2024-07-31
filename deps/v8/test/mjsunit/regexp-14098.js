// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --enable-experimental-regexp-engine
// Flags: --no-default-to-experimental-regexp-engine

// An example where the Experimental regexp engine
// used to disagree with the backtracking engine and the specification
// for nullable quantifiers.
// v8:14098
var str = "ab";

// executed by the Experimental engine (l flag)
var exp_repetitions = /(?:a?b??){0,2}/l;
var exp_empty_star = /(?:a?b??)*/l;

// executed by the backtracking engine
var bt_empty_star = /(?:a?b??)*/;

// The engines should both match "ab":
// The first star repetition should match 'a'
// because ignoring 'b' has highest-priority.
// Then the greedy star should be entered again.
// Then the second repetition should match 'b',
// a non-empty repetition of the star.

assertEquals(["ab"], bt_empty_star.exec(str));
assertEquals(['ab'], exp_empty_star.exec(str));
assertEquals(['ab'], exp_repetitions.exec(str));
