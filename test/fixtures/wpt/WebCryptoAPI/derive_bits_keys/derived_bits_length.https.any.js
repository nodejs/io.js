// META: title=WebCryptoAPI: deriveBits() tests for the 'length' parameter
// META: script=derived_bits_length.js
// META: script=derived_bits_length_vectors.js
// META: script=derived_bits_length_testcases.js

// Define subtests from a `promise_test` to ensure the harness does not
// complete before the subtests are available. `explicit_done` cannot be used
// for this purpose because the global `done` function is automatically invoked
// by the WPT infrastructure in dedicated worker tests defined using the
// "multi-global" pattern.
promise_test(define_tests, 'setup - define tests');
