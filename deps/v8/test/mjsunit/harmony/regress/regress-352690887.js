// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let set = new Set([1, 2, 3]);

let setLike = {
  size: 0,
  has() {
    throw new Error("Unexpected call to |has| method");
  },
  keys() {
    return {
      get next() {
        // Clear the set when getting the |next| method.
        set.clear();

        // And then add a single new key.
        set.add(4);

        return function() {
          return {done: true};
        };
      }
    };
  },
};

// The result should consist of the single, newly added key.
let result = set.union(setLike);
const resultArray = Array.from(result);
assertEquals(resultArray, [4]);
