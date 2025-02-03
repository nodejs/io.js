// META: global=window,dedicatedworker,shadowrealm
// META: title=Encoding API: TextDecoder ignoreBOM option

var cases = [
    {encoding: 'utf-8', bytes: [0xEF, 0xBB, 0xBF, 0x61, 0x62, 0x63]},
    {encoding: 'utf-16le', bytes: [0xFF, 0xFE, 0x61, 0x00, 0x62, 0x00, 0x63, 0x00]},
    {encoding: 'utf-16be', bytes: [0xFE, 0xFF, 0x00, 0x61, 0x00, 0x62, 0x00, 0x63]}
];

cases.forEach(function(testCase) {
    test(function() {
        var BOM = '\uFEFF';
        var decoder = new TextDecoder(testCase.encoding, {ignoreBOM: true});
        var bytes = new Uint8Array(testCase.bytes);
        assert_equals(
            decoder.decode(bytes),
            BOM + 'abc',
            testCase.encoding + ': BOM should be present in decoded string if ignored');
        assert_equals(
            decoder.decode(bytes),
            BOM + 'abc',
            testCase.encoding + ': BOM should be present in decoded string if ignored by a reused decoder');

        decoder = new TextDecoder(testCase.encoding, {ignoreBOM: false});
        assert_equals(
            decoder.decode(bytes),
            'abc',
            testCase.encoding + ': BOM should be absent from decoded string if not ignored');
        assert_equals(
            decoder.decode(bytes),
            'abc',
            testCase.encoding + ': BOM should be absent from decoded string if not ignored by a reused decoder');

        decoder = new TextDecoder(testCase.encoding);
        assert_equals(
            decoder.decode(bytes),
            'abc',
            testCase.encoding + ': BOM should be absent from decoded string by default');
        assert_equals(
            decoder.decode(bytes),
            'abc',
            testCase.encoding + ': BOM should be absent from decoded string by default with a reused decoder');
    }, 'BOM is ignored if ignoreBOM option is specified: ' + testCase.encoding);
});

test(function() {
    assert_true('ignoreBOM' in new TextDecoder(), 'The ignoreBOM attribute should exist on TextDecoder.');
    assert_equals(typeof  new TextDecoder().ignoreBOM, 'boolean', 'The type of the ignoreBOM attribute should be boolean.');
    assert_false(new TextDecoder().ignoreBOM, 'The ignoreBOM attribute should default to false.');
    assert_true(new TextDecoder('utf-8', {ignoreBOM: true}).ignoreBOM, 'The ignoreBOM attribute can be set using an option.');

}, 'The ignoreBOM attribute of TextDecoder');
