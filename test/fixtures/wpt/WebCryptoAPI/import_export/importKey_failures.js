function run_test(algorithmNames) {
    var subtle = crypto.subtle; // Change to test prefixed implementations

    setup({explicit_timeout: true});

// These tests check that importKey and exportKey throw an error, and that
// the error is of the right type, for a wide set of incorrect parameters.

// Error testing occurs by setting the parameter that should trigger the
// error to an invalid value, then combining that with all valid
// parameters that should be checked earlier by importKey, and all
// valid and invalid parameters that should be checked later by
// importKey.
//
// There are a lot of combinations of possible parameters for both
// success and failure modes, resulting in a very large number of tests
// performed.


    var allTestVectors = [ // Parameters that should work for importKey / exportKey
        {name: "Ed25519", privateUsages: ["sign"], publicUsages: ["verify"]},
        {name: "Ed448", privateUsages: ["sign"], publicUsages: ["verify"]},
        {name: "ECDSA", privateUsages: ["sign"], publicUsages: ["verify"]},
        {name: "X25519",  privateUsages: ["deriveKey", "deriveBits"], publicUsages: []},
        {name: "X448",  privateUsages: ["deriveKey", "deriveBits"], publicUsages: []},
        {name: "ECDH",  privateUsages: ["deriveKey", "deriveBits"], publicUsages: []}
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

    function parameterString(format, algorithm, extractable, usages, data) {
        if (typeof algorithm !== "object" && typeof algorithm !== "string") {
            alert(algorithm);
        }

        var jwk_label = "";
        if (format === "jwk")
            jwk_label = data.d === undefined ? " (public) " : "(private)";

        var result = "(" +
                        objectToString(format) + jwk_label + ", " +
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
    function testError(format, algorithm, keyData, keySize, usages, extractable, expectedError, testTag) {
        promise_test(async() => {
            let key;
            try {
                key = await subtle.importKey(format, keyData, algorithm, extractable, usages);
            } catch(err) {
                let actualError = typeof expectedError === "number" ? err.code : err.name;
                assert_equals(actualError, expectedError, testTag + " not supported.");
            }
            assert_equals(key, undefined, "Operation succeeded, but should not have.");
        }, testTag + ": importKey" + parameterString(format, algorithm, extractable, usages, keyData));
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

        var goodUsageCombinations = validUsages.length === 0 ? [] : allValidUsages(validUsages, false, mandatoryUsages);

        illegalUsages.forEach(function(illegalUsage) {
            results.push([illegalUsage]);
            goodUsageCombinations.forEach(function(usageCombination) {
                results.push(usageCombination.concat([illegalUsage]));
            });
        });

        return results;
    }

    function validUsages(usages, format, data) {
        if (format === 'spki' || format === 'raw') return usages.publicUsages
        if (format === 'pkcs8') return usages.privateUsages
        if (format === 'jwk') {
            if (data === undefined)
                return [];
            return data.d === undefined ? usages.publicUsages : usages.privateUsages;
        }
        return [];
    }

    function isPrivateKey(data) {
        return data.d !== undefined;
    }

// Now test for properly handling errors
// - Unsupported algorithm
// - Bad usages for algorithm
// - Bad key lengths
// - Lack of a mandatory format field
// - Incompatible keys pair

    // Algorithms normalize okay, but usages bad (though not empty).
    // It shouldn't matter what other extractable is. Should fail
    // due to SyntaxError
    testVectors.forEach(function(vector) {
        var name = vector.name;
        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            getValidKeyData(algorithm).forEach(function(test) {
                invalidUsages(validUsages(vector, test.format, test.data)).forEach(function(usages) {
                    [true, false].forEach(function(extractable) {
                        testError(test.format, algorithm, test.data, name, usages, extractable, "SyntaxError", "Bad usages");
                    });
                });
            });
        });
    });

    // Algorithms normalize okay, but usages bad (empty).
    // Should fail due to SyntaxError
    testVectors.forEach(function(vector) {
        var name = vector.name;
        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            getValidKeyData(algorithm).filter((test) => test.format === 'pkcs8' || (test.format === 'jwk' && isPrivateKey(test.data))).forEach(function(test) {
                [true, false].forEach(function(extractable) {
                    testError(test.format, algorithm, test.data, name, [/* Empty usages */], extractable, "SyntaxError", "Empty usages");
                });
            });
        });
    });

    // Algorithms normalize okay, usages ok. The length of the key must throw a DataError exception.
    testVectors.forEach(function(vector) {
        var name = vector.name;
        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            getBadKeyLengthData(algorithm).forEach(function(test) {
                allValidUsages(validUsages(vector, test.format, test.data)).forEach(function(usages) {
                    [true, false].forEach(function(extractable) {
                        testError(test.format, algorithm, test.data, name, usages, extractable, "DataError", "Bad key length");
                    });
                });
            });
        });
    });

    // Algorithms normalize okay, usages ok and valid key. The lack of the mandatory JWK parameter must throw a DataError exception.
    testVectors.forEach(function(vector) {
        var name = vector.name;
        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
           getMissingJWKFieldKeyData(algorithm).forEach(function(test) {
                allValidUsages(validUsages(vector, 'jwk', test.data)).forEach(function(usages) {
                    [true, false].forEach(function(extractable) {
                        testError('jwk', algorithm, test.data, name, usages, extractable, "DataError", "Missing JWK '" + test.param + "' parameter");
                    });
                });
            });
        });
    });

    // Algorithms normalize okay, usages ok and valid key. The public key is not compatible with the private key.
    testVectors.forEach(function(vector) {
        var name = vector.name;
        allAlgorithmSpecifiersFor(name).forEach(function(algorithm) {
            getMismatchedJWKKeyData(algorithm).forEach(function(data) {
                allValidUsages(vector.privateUsages).forEach(function(usages) {
                    [true].forEach(function(extractable) {
                        testError('jwk', algorithm, data, name, usages, extractable, "DataError", "Invalid key pair");
                    });
                });
            });
        });
    });
}
