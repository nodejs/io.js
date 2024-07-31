// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-struct

let mutex = new Atomics.Mutex;
let locked_count = 0;

assertEquals(42, Atomics.Mutex.lock(mutex, () => {
  locked_count++; return 42;
}));
assertEquals(locked_count, 1);

let lockResult = Atomics.Mutex.tryLock(mutex, () => {
  locked_count++;
});
assertEquals(true, lockResult.success);
assertEquals(locked_count, 2);

// Recursively locking throws.
Atomics.Mutex.lock(mutex, () => {
  locked_count++;
  assertThrows(() => {
    Atomics.Mutex.lock(mutex, () => { throw "unreachable"; });
  }, Error);
});
assertEquals(locked_count, 3);

// Recursive tryLocking returns returns false success value.
Atomics.Mutex.lock(mutex, () => {
  locked_count++;
  lockResult = Atomics.Mutex.tryLock(mutex, () => {
    throw 'unreachable';
  });
  assertEquals(false, lockResult.success);
  assertEquals(undefined, lockResult.value);
});
assertEquals(locked_count, 4);

// Throwing in the callback should unlock the mutex.
assertThrowsEquals(() => {
  Atomics.Mutex.lock(mutex, () => { throw 42; });
}, 42);
Atomics.Mutex.lock(mutex, () => { locked_count++; });
assertEquals(locked_count, 5);

assertThrowsEquals(() => {
  Atomics.Mutex.tryLock(mutex, () => { throw 42; });
}, 42);
Atomics.Mutex.tryLock(mutex, () => { locked_count++; });
assertEquals(locked_count, 6);

// Mutexes can be assigned to shared objects.
(function TestMutexCanBeAssignedToSharedObjects() {
  const Box = new SharedStructType(["payload"]);
  const box = new Box;
  box.payload = mutex;
})();

// lockWithTimeout's return value is an object with a value field
// and a success field.
lockResult = Atomics.Mutex.lockWithTimeout(mutex, () => 42, 1);
assertEquals(42, lockResult.value);
assertEquals(true, lockResult.success);

// Timeout must be a number.
assertThrows(() => {
  Atomics.Mutex.lockWithTimeout(mutex, () => {}, '42');
})
assertThrows(() => {
  Atomics.Mutex.lockWithTimeout(mutex, () => {}, {});
})
assertThrows(() => {
  Atomics.Mutex.lockWithTimeout(mutex, () => {});
})
