// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Negated empty class matches anything.
assertTrue(/^[^]*$/v.test('asdf'));
assertTrue(/^[^]*$/v.test(''));
assertTrue(/^[^]*$/v.test('🤯'));
assertTrue(/^(([^]+?)*)$/v.test('asdf'));
assertTrue(/^(([^]+?)*)$/v.test(''));
assertTrue(/^(([^]+?)*)$/v.test('🤯'));
