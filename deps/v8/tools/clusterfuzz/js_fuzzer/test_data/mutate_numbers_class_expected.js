// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original: mutate_numbers_class.js
class A {
  get
  /* NumberMutator: Replaced 0 with 5 */
  5() {}

  set
  /* NumberMutator: Replaced 0 with 4 */
  4(val) {}

  get
  /* NumberMutator: Replaced 1 with 3 */
  3() {}

  set
  /* NumberMutator: Replaced 1 with 5 */
  5(val) {}

}
