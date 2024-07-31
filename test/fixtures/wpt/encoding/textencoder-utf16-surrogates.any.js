// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: USVString surrogate handling when encoding

var bad = [
    {
        input: '\uD800',
        expected: '\uFFFD',
        name: 'lone surrogate lead'
    },
    {
        input: '\uDC00',
        expected: '\uFFFD',
        name: 'lone surrogate trail'
    },
    {
        input: '\uD800\u0000',
        expected: '\uFFFD\u0000',
        name: 'unmatched surrogate lead'
    },
    {
        input: '\uDC00\u0000',
        expected: '\uFFFD\u0000',
        name: 'unmatched surrogate trail'
    },
    {
        input: '\uDC00\uD800',
        expected: '\uFFFD\uFFFD',
        name: 'swapped surrogate pair'
    },
    {
        input: '\uD834\uDD1E',
        expected: '\uD834\uDD1E',
        name: 'properly encoded MUSICAL SYMBOL G CLEF (U+1D11E)'
    }
];

bad.forEach(function(t) {
    test(function() {
        var encoded = new TextEncoder().encode(t.input);
        var decoded = new TextDecoder().decode(encoded);
        assert_equals(decoded, t.expected);
    }, 'USVString handling: ' + t.name);
});

test(function() {
    assert_equals(new TextEncoder().encode().length, 0, 'Should default to empty string');
}, 'USVString default');
