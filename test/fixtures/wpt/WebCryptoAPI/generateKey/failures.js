function run_test(algorithmNames) {
    var subtle = crypto.subtle; // Change to test prefixed implementations

    setup({explicit_timeout: true});

// These tests check that generateKey throws an error, and that
// the error is of the right type, for a wide set of incorrect parameters.
//
// Error testing occurs by setting the parameter that should trigger the
// error to an invalid value, then combining that with all valid
// parameters that should be checked earlier by generateKey, and all
// valid and invalid parameters that should be checked later by
// generateKey.
//
// There are a lot of combinations of possible parameters for both
// success and failure modes, resulting in a very large number of tests
// performed.


// Setup: define the correct behaviors that should be sought, and create
// helper functions that generate all possible test parameters for
// different situations.

    var allTestVectors = [ // Parameters that should work for generateKey
        {name: "AES-CTR",  resultType: CryptoKey, usages: ["encrypt", "decrypt", "wrapKey", "unwrapKey"], mandatoryUsages: []},
        {name: "AES-CBC",  resultType: CryptoKey, usages: ["encrypt", "decrypt", "wrapKey", "unwrapKey"], mandatoryUsages: []},
        {name: "AES-GCM",  resultType: CryptoKey, usages: ["encrypt", "decrypt", "wrapKey", "unwrapKey"], mandatoryUsages: []},
        {name: "AES-KW",   resultType: CryptoKey, usages: ["wrapKey", "unwrapKey"], mandatoryUsages: []},
        {name: "HMAC",     resultType: CryptoKey, usages: ["sign", "verify"], mandatoryUsages: []},
        {name: "RSASSA-PKCS1-v1_5", resultType: "CryptoKeyPair", usages: ["sign", "verify"], mandatoryUsages: ["sign"]},
        {name: "RSA-PSS",  resultType: "CryptoKeyPair", usages: ["sign", "verify"], mandatoryUsages: ["sign"]},
        {name: "RSA-OAEP", resultType: "CryptoKeyPair", usages: ["encrypt", "decrypt", "wrapKey", "unwrapKey"], mandatoryUsages: ["decrypt", "unwrapKey"]},
        {name: "ECDSA",    resultType: "CryptoKeyPair", usages: ["sign", "verify"], mandatoryUsages: ["sign"]},
        {name: "ECDH",     resultType: "CryptoKeyPair", usages: ["deriveKey", "deriveBits"], mandatoryUsages: ["deriveKey", "deriveBits"]},
        {name: "Ed25519",  resultType: "CryptoKeyPair", usages: ["sign", "verify"], mandatoryUsages: ["sign"]},
        {name: "Ed448",    resultType: "CryptoKeyPair", usages: ["sign", "verify"], mandatoryUsages: ["sign"]},
        {name: "X25519",   resultType: "CryptoKeyPair", usages: ["deriveKey", "deriveBits"], mandatoryUsages: ["deriveKey", "deriveBits"]},
        {name: "X448",     resultType: "CryptoKeyPair", usages: ["deriveKey", "deriveBits"], mandatoryUsages: ["deriveKey", "deriveBits"]},
    ];

    var testVectors = [];
    if (algorithmNames && !Array.isArray(algorithmNames)) {
        algorithmNames = [algorithmNames];
    };
    allTestVectors.forEach(function(vector) {
        if (!algorithmNames || algorithmNames.includes(vector.name)) {
            testVectors.push(vector);
        }
    });


    function parameterString(algorithm, extractable, usages) {
        if (typeof algorithm !== "object" && typeof algorithm !== "string") {
            alert(algorithm);
        }

        var result = "(" +
                        objectToString(algorithm) + ", " +
                        objectToString(extractable) + ", " +
                        objectToString(usages) +
                     ")";

        return result;
    }

    // Test that a given combination of parameters results in an error,
    // AND that it is the correct kind of error.
    //
    // Expected error is either a number, tested against the error code,
    // or a string, tested against the error name.
    function testError(algorithm, extractable, usages, expectedError, testTag) {
        promise_test(function(test) {
            return crypto.subtle.generateKey(algorithm, extractable, usages)
            .then(function(result) {
                assert_unreached("Operation succeeded, but should not have");
            }, function(err) {
                if (typeof expectedError === "number") {
                    assert_equals(err.code, expectedError, testTag + " not supported");
                } else {
                    assert_equals(err.name, expectedError, testTag + " not supported");
                }
            });
        }, testTag + ": generateKey" + parameterString(algorithm, extractable, usages));
    }


    // Given an algorithm name, create several invalid parameters.
    function badAlgorithmPropertySpecifiersFor(algorithmName) {
        var results = [];

        if (algorithmName.toUpperCase().substring(0, 3) === "AES") {
            // Specifier properties are name and length
            [64, 127, 129, 255, 257, 512].forEach(function(length) {
                results.push({name: algorithmName, length: length});
            });
        } else if (algorithmName.toUpperCase().substring(0, 3) === "RSA") {
            [new Uint8Array([1]), new Uint8Array([1,0,0])].forEach(function(publicExponent) {
                results.push({name: algorithmName, hash: "SHA-256", modulusLength: 1024, publicExponent: publicExponent});
            });
        } else if (algorithmName.toUpperCase().substring(0, 2) === "EC") {
            ["P-512", "Curve25519"].forEach(function(curveName) {
                results.push({name: algorithmName, namedCurve: curveName});
            });
        }

        return results;
    }


    // Don't create an exhaustive list of all invalid usages,
    // because there would usually be nearly 2**8 of them,
    // way too many to test. Instead, create every singleton
    // of an illegal usage, and "poison" every valid usage
    // with an illegal one.
    function invalidUsages(validUsages, mandatoryUsages) {
        var results = [];

        var illegalUsages = [];
        ["encrypt", "decrypt", "sign", "verify", "wrapKey", "unwrapKey", "deriveKey", "deriveBits"].forEach(function(usage) {
            if (!validUsages.includes(usage)) {
                illegalUsages.push(usage);
            }
        });

        var goodUsageCombinations = allValidUsages(validUsages, false, mandatoryUsages);

        illegalUsages.forEach(function(illegalUsage) {
            results.push([illegalUsage]);
            goodUsageCombinations.forEach(function(usageCombination) {
                results.push(usageCombination.concat([illegalUsage]));
            });
        });

        return results;
    }


// Now test for properly handling errors
// - Unsupported algorithm
// - Bad usages for algorithm
// - Bad key lengths

    // Algorithm normalization should fail with "Not supported"
    var badAlgorithmNames = [
        "AES",
        {name: "AES"},
        {name: "AES", length: 128},
        {name: "AES-CMAC", length: 128},    // Removed after CR
        {name: "AES-CFB", length: 128},      // Removed after CR
        {name: "HMAC", hash: "MD5"},
        {name: "RSA", hash: "SHA-256", modulusLength: 2048, publicExponent: new Uint8Array([1,0,1])},
        {name: "RSA-PSS", hash: "SHA", modulusLength: 2048, publicExponent: new Uint8Array([1,0,1])},
        {name: "EC", namedCurve: "P521"}
    ];


    // Algorithm normalization failures should be found first
    // - all other parameters can be good or bad, should fail
    //   due to NotSupportedError.
    badAlgorithmNames.forEach(function(algorithm) {
        allValidUsages(["decrypt", "sign", "deriveBits"], true, []) // Small search space, shouldn't matter because should fail before used
        .forEach(function(usages) {
            [false, true, "RED", 7].forEach(function(extractable){
                testError(algorithm, extractable, usages, "NotSupportedError", "Bad algorithm");
            });
        });
    });

    // Empty algorithm should fail with TypeError
    allValidUsages(["decrypt", "sign", "deriveBits"], true, []) // Small search space, shouldn't matter because should fail before used
        .forEach(function(usages) {
            [false, true, "RED", 7].forEach(function(extractable){
                testError({}, extractable, usages, "TypeError", "Empty algorithm");
            });
        });


    // Algorithms normalize okay, but usages bad (though not empty).
    // It shouldn't matter what other extractable is. Should fail
    // due to SyntaxError
    testVectors.forEach(function(vector) {
        var name = vector.name;

        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            invalidUsages(vector.usages, vector.mandatoryUsages).forEach(function(usages) {
                [true].forEach(function(extractable) {
                    testError(algorithm, extractable, usages, "SyntaxError", "Bad usages");
                });
            });
        });
    });


    // Other algorithm properties should be checked next, so try good
    // algorithm names and usages, but bad algorithm properties next.
    // - Special case: normally bad usage [] isn't checked until after properties,
    //   so it's included in this test case. It should NOT cause an error.
    testVectors.forEach(function(vector) {
        var name = vector.name;
        badAlgorithmPropertySpecifiersFor(name).forEach(function(algorithm) {
            allValidUsages(vector.usages, true, vector.mandatoryUsages)
            .forEach(function(usages) {
                [false, true].forEach(function(extractable) {
                    if (name.substring(0,2) === "EC") {
                        testError(algorithm, extractable, usages, "NotSupportedError", "Bad algorithm property");
                    } else {
                        testError(algorithm, extractable, usages, "OperationError", "Bad algorithm property");
                    }
                });
            });
        });
    });


    // The last thing that should be checked is empty usages (disallowed for secret and private keys).
    testVectors.forEach(function(vector) {
        var name = vector.name;

        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            var usages = [];
            [false, true].forEach(function(extractable) {
                testError(algorithm, extractable, usages, "SyntaxError", "Empty usages");
            });
        });
    });


}
