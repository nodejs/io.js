function define_tests() {
    // May want to test prefixed implementations.
    var subtle = self.crypto.subtle;

    Object.keys(testCases).forEach(algorithm => {
        let testData = algorithms[algorithm];
        testCases[algorithm].forEach(testParam => {
            promise_test(async() => {
                let derivedBits, privateKey, publicKey;
                try {
                    privateKey = await subtle.importKey(testData.privateKey.format, testData.privateKey.data, testData.importAlg, false, ["deriveBits"]);
                    if (testData.deriveAlg.public !== undefined) {
                        publicKey = await subtle.importKey(testData.publicKey.format, testData.publicKey.data, testData.importAlg, false, []);
                        testData.deriveAlg.public = publicKey;
                    }
                    if (testParam.length === "omitted")
                        derivedBits = await subtle.deriveBits(testData.deriveAlg, privateKey);
                    else
                        derivedBits = await subtle.deriveBits(testData.deriveAlg, privateKey, testParam.length);
                    if (testParam.expected === undefined) {
                        assert_unreached("deriveBits should have thrown an OperationError exception.");
                    }
                    assert_array_equals(new Uint8Array(derivedBits), testParam.expected, "Derived bits do not match the expected result.");
                } catch (err) {
                    if (err instanceof AssertionError || testParam.expected !== undefined) {
                        throw err;
                    }
                    assert_true(privateKey !== undefined, "Key should be valid.");
                    assert_equals(err.name, "OperationError", "deriveBits correctly threw OperationError: " + err.message);
                }
            }, algorithm + " derivation with " + testParam.length + " as 'length' parameter");
        });
    });

    return Promise.resolve("define_tests");
}
