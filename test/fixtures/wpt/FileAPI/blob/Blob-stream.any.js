// META: title=Blob Stream
// META: script=../support/Blob.js
// META: script=/common/gc.js
'use strict';

// Helper function that triggers garbage collection while reading a chunk
// if perform_gc is true.
async function read_and_gc(reader, perform_gc) {
  // Passing Uint8Array for byte streams; non-byte streams will simply ignore it
  const read_promise = reader.read(new Uint8Array(64));
  if (perform_gc) {
    await garbageCollect();
  }
  return read_promise;
}

// Takes in a ReadableStream and reads from it until it is done, returning
// an array that contains the results of each read operation. If perform_gc
// is true, garbage collection is triggered while reading every chunk.
async function read_all_chunks(stream, { perform_gc = false, mode } = {}) {
  assert_true(stream instanceof ReadableStream);
  assert_true('getReader' in stream);
  const reader = stream.getReader({ mode });

  assert_true('read' in reader);
  let read_value = await read_and_gc(reader, perform_gc);

  let out = [];
  let i = 0;
  while (!read_value.done) {
    for (let val of read_value.value) {
      out[i++] = val;
    }
    read_value = await read_and_gc(reader, perform_gc);
  }
  return out;
}

promise_test(async () => {
  const blob = new Blob(["PASS"]);
  const stream = blob.stream();
  const chunks = await read_all_chunks(stream);
  for (let [index, value] of chunks.entries()) {
    assert_equals(value, "PASS".charCodeAt(index));
  }
}, "Blob.stream()")

promise_test(async () => {
  const blob = new Blob();
  const stream = blob.stream();
  const chunks = await read_all_chunks(stream);
  assert_array_equals(chunks, []);
}, "Blob.stream() empty Blob")

promise_test(async () => {
  const input_arr = [8, 241, 48, 123, 151];
  const typed_arr = new Uint8Array(input_arr);
  const blob = new Blob([typed_arr]);
  const stream = blob.stream();
  const chunks = await read_all_chunks(stream);
  assert_array_equals(chunks, input_arr);
}, "Blob.stream() non-unicode input")

promise_test(async() => {
  const input_arr = [8, 241, 48, 123, 151];
  const typed_arr = new Uint8Array(input_arr);
  let blob = new Blob([typed_arr]);
  const stream = blob.stream();
  blob = null;
  await garbageCollect();
  const chunks = await read_all_chunks(stream, { perform_gc: true });
  assert_array_equals(chunks, input_arr);
}, "Blob.stream() garbage collection of blob shouldn't break stream " +
      "consumption")

promise_test(async() => {
  const input_arr = [8, 241, 48, 123, 151];
  const typed_arr = new Uint8Array(input_arr);
  let blob = new Blob([typed_arr]);
  const chunksPromise = read_all_chunks(blob.stream());
  // It somehow matters to do GC here instead of doing `perform_gc: true`
  await garbageCollect();
  assert_array_equals(await chunksPromise, input_arr);
}, "Blob.stream() garbage collection of stream shouldn't break stream " +
      "consumption")

promise_test(async () => {
  const input_arr = [8, 241, 48, 123, 151];
  const typed_arr = new Uint8Array(input_arr);
  let blob = new Blob([typed_arr]);
  const stream = blob.stream();
  const chunks = await read_all_chunks(stream, { mode: "byob" });
  assert_array_equals(chunks, input_arr);
}, "Reading Blob.stream() with BYOB reader")
