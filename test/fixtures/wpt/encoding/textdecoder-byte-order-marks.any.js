// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: Byte-order marks

var testCases = [
    {
        encoding: 'utf-8',
        bom: [0xEF, 0xBB, 0xBF],
        bytes: [0x7A, 0xC2, 0xA2, 0xE6, 0xB0, 0xB4, 0xF0, 0x9D, 0x84, 0x9E, 0xF4, 0x8F, 0xBF, 0xBD]
    },
    {
        encoding: 'utf-16le',
        bom: [0xff, 0xfe],
        bytes: [0x7A, 0x00, 0xA2, 0x00, 0x34, 0x6C, 0x34, 0xD8, 0x1E, 0xDD, 0xFF, 0xDB, 0xFD, 0xDF]
    },
    {
        encoding: 'utf-16be',
        bom: [0xfe, 0xff],
        bytes: [0x00, 0x7A, 0x00, 0xA2, 0x6C, 0x34, 0xD8, 0x34, 0xDD, 0x1E, 0xDB, 0xFF, 0xDF, 0xFD]
    }
];

var string = 'z\xA2\u6C34\uD834\uDD1E\uDBFF\uDFFD'; // z, cent, CJK water, G-Clef, Private-use character

testCases.forEach(function(t) {
    test(function() {

        var decoder = new TextDecoder(t.encoding);
        assert_equals(decoder.decode(new Uint8Array(t.bytes)), string,
                      'Sequence without BOM should decode successfully');

        assert_equals(decoder.decode(new Uint8Array(t.bom.concat(t.bytes))), string,
                      'Sequence with BOM should decode successfully (with no BOM present in output)');

        testCases.forEach(function(o) {
            if (o === t)
                return;

            assert_not_equals(decoder.decode(new Uint8Array(o.bom.concat(t.bytes))), string,
                              'Mismatching BOM should not be ignored - treated as garbage bytes.');
        });

    }, 'Byte-order marks: ' + t.encoding);
});
