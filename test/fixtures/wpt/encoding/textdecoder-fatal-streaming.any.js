// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: End-of-file

test(function() {
    [
        {encoding: 'utf-8', sequence: [0xC0]},
        {encoding: 'utf-16le', sequence: [0x00]},
        {encoding: 'utf-16be', sequence: [0x00]}
    ].forEach(function(testCase) {

        assert_throws_js(TypeError, function() {
            var decoder = new TextDecoder(testCase.encoding, {fatal: true});
            decoder.decode(new Uint8Array(testCase.sequence));
        }, 'Unterminated ' + testCase.encoding + ' sequence should throw if fatal flag is set');

        assert_equals(
            new TextDecoder(testCase.encoding).decode(new Uint8Array([testCase.sequence])),
            '\uFFFD',
            'Unterminated UTF-8 sequence should emit replacement character if fatal flag is unset');
    });
}, 'Fatal flag, non-streaming cases');

test(function() {

    var decoder = new TextDecoder('utf-16le', {fatal: true});
    var odd = new Uint8Array([0x00]);
    var even = new Uint8Array([0x00, 0x00]);

    assert_equals(decoder.decode(odd, {stream: true}), '');
    assert_equals(decoder.decode(odd), '\u0000');

    assert_throws_js(TypeError, function() {
        decoder.decode(even, {stream: true});
        decoder.decode(odd)
    });

    assert_throws_js(TypeError, function() {
        decoder.decode(odd, {stream: true});
        decoder.decode(even);
    });

    assert_equals(decoder.decode(even, {stream: true}), '\u0000');
    assert_equals(decoder.decode(even), '\u0000');

}, 'Fatal flag, streaming cases');
