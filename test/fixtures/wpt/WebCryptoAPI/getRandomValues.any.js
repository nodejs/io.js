// Step 1.
test(function() {
    assert_throws_dom("TypeMismatchError", function() {
        self.crypto.getRandomValues(new Float16Array(6))
    }, "Float16Array")

    assert_throws_dom("TypeMismatchError", function() {
        const len = 65536 / Float16Array.BYTES_PER_ELEMENT + 1;
        self.crypto.getRandomValues(new Float16Array(len));
    }, "Float16Array (too long)")
}, "Float16 arrays");

test(function() {
    assert_throws_dom("TypeMismatchError", function() {
        self.crypto.getRandomValues(new Float32Array(6))
    }, "Float32Array")
    assert_throws_dom("TypeMismatchError", function() {
        self.crypto.getRandomValues(new Float64Array(6))
    }, "Float64Array")

    assert_throws_dom("TypeMismatchError", function() {
        const len = 65536 / Float32Array.BYTES_PER_ELEMENT + 1;
        self.crypto.getRandomValues(new Float32Array(len));
    }, "Float32Array (too long)")
    assert_throws_dom("TypeMismatchError", function() {
        const len = 65536 / Float64Array.BYTES_PER_ELEMENT + 1;
        self.crypto.getRandomValues(new Float64Array(len))
    }, "Float64Array (too long)")
}, "Float arrays");

test(function() {
    assert_throws_dom("TypeMismatchError", function() {
        self.crypto.getRandomValues(new DataView(new ArrayBuffer(6)))
    }, "DataView")

    assert_throws_dom("TypeMismatchError", function() {
        self.crypto.getRandomValues(new DataView(new ArrayBuffer(65536 + 1)))
    }, "DataView (too long)")
}, "DataView");

const arrays = [
    'Int8Array',
    'Int16Array',
    'Int32Array',
    'BigInt64Array',
    'Uint8Array',
    'Uint8ClampedArray',
    'Uint16Array',
    'Uint32Array',
    'BigUint64Array',
];

for (const array of arrays) {
    const ctor = globalThis[array];

    test(function() {
        assert_equals(self.crypto.getRandomValues(new ctor(8)).constructor,
                      ctor, "crypto.getRandomValues(new " + array + "(8))")
    }, "Integer array: " + array);

    test(function() {
        const maxlength = 65536 / ctor.BYTES_PER_ELEMENT;
        assert_throws_dom("QuotaExceededError", function() {
            self.crypto.getRandomValues(new ctor(maxlength + 1))
        }, "crypto.getRandomValues length over 65536")
    }, "Large length: " + array);

    test(function() {
        assert_true(self.crypto.getRandomValues(new ctor(0)).length == 0)
    }, "Null arrays: " + array);

    test(function() {
        class Buffer extends ctor {}
        // Must not throw for the test to pass
        self.crypto.getRandomValues(new Buffer(256));
    }, "Subclass of " + array);
}
