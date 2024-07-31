// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: Invalid UTF-16 surrogates with UTF-8 encoding

var badStrings = [
    {
        input: 'abc123',
        expected: [0x61, 0x62, 0x63, 0x31, 0x32, 0x33],
        decoded: 'abc123',
        name: 'Sanity check'
    },
    {
        input: '\uD800',
        expected: [0xef, 0xbf, 0xbd],
        decoded: '\uFFFD',
        name: 'Surrogate half (low)'
    },
    {
        input: '\uDC00',
        expected: [0xef, 0xbf, 0xbd],
        decoded: '\uFFFD',
        name: 'Surrogate half (high)'
    },
    {
        input: 'abc\uD800123',
        expected: [0x61, 0x62, 0x63, 0xef, 0xbf, 0xbd, 0x31, 0x32, 0x33],
        decoded: 'abc\uFFFD123',
        name: 'Surrogate half (low), in a string'
    },
    {
        input: 'abc\uDC00123',
        expected: [0x61, 0x62, 0x63, 0xef, 0xbf, 0xbd, 0x31, 0x32, 0x33],
        decoded: 'abc\uFFFD123',
        name: 'Surrogate half (high), in a string'
    },
    {
        input: '\uDC00\uD800',
        expected: [0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd],
        decoded: '\uFFFD\uFFFD',
        name: 'Wrong order'
    }
];

badStrings.forEach(function(t) {
    test(function() {
        var encoded = new TextEncoder().encode(t.input);
        assert_array_equals([].slice.call(encoded), t.expected);
        assert_equals(new TextDecoder('utf-8').decode(encoded), t.decoded);
    }, 'Invalid surrogates encoded into UTF-8: ' + t.name);
});
