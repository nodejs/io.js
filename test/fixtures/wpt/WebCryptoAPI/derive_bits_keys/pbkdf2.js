function define_tests() {
    // May want to test prefixed implementations.
    var subtle = self.crypto.subtle;

    // pbkdf2_vectors sets up test data with the correct derivations for each
    // test case.
    var testData = getTestData();
    var passwords = testData.passwords;
    var salts = testData.salts;
    var derivations = testData.derivations;

    // What kinds of keys can be created with deriveKey? The following:
    var derivedKeyTypes = testData.derivedKeyTypes;

    return setUpBaseKeys(passwords)
    .then(function(allKeys) {
        // We get several kinds of base keys. Normal ones that can be used for
        // derivation operations, ones that lack the deriveBits usage, ones
        // that lack the deriveKeys usage, and one key that is for the wrong
        // algorithm (not PBKDF2 in this case).
        var baseKeys = allKeys.baseKeys;
        var noBits = allKeys.noBits;
        var noKey = allKeys.noKey;
        var wrongKey = allKeys.wrongKey;

        // Test each combination of password size, salt size, hash function,
        // and number of iterations. The derivations object is structured in
        // that way, so navigate it to run tests and compare with correct results.
        Object.keys(derivations).forEach(function(passwordSize) {
            Object.keys(derivations[passwordSize]).forEach(function(saltSize) {
                Object.keys(derivations[passwordSize][saltSize]).forEach(function(hashName) {
                    Object.keys(derivations[passwordSize][saltSize][hashName]).forEach(function(iterations) {
                        var testName = passwordSize + " password, " + saltSize + " salt, " + hashName + ", with " + iterations + " iterations";

                        // Check for correct deriveBits result
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, baseKeys[passwordSize], 256)
                            .then(function(derivation) {
                                assert_true(equalBuffers(derivation, derivations[passwordSize][saltSize][hashName][iterations]), "Derived correct key");
                            }, function(err) {
                                assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                            });
                        }, testName);

                        // 0 length
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, baseKeys[passwordSize], 0)
                            .then(function(derivation) {
                                assert_true(equalBuffers(derivation.byteLength, 0, "Derived correctly empty key"));
                            }, function(err) {
                                assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                            });
                        }, testName + " with 0 length");

                        // Check for correct deriveKey results for every kind of
                        // key that can be created by the deriveKeys operation.
                        derivedKeyTypes.forEach(function(derivedKeyType) {
                            var testName = "Derived key of type ";
                            Object.keys(derivedKeyType.algorithm).forEach(function(prop) {
                                testName += prop + ": " + derivedKeyType.algorithm[prop] + " ";
                            });
                            testName += " using " + passwordSize + " password, " + saltSize + " salt, " + hashName + ", with " + iterations + " iterations";

                            // Test the particular key derivation.
                            subsetTest(promise_test, function(test) {
                                return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, baseKeys[passwordSize], derivedKeyType.algorithm, true, derivedKeyType.usages)
                                .then(function(key) {
                                    // Need to export the key to see that the correct bits were set.
                                    return subtle.exportKey("raw", key)
                                    .then(function(buffer) {
                                        assert_true(equalBuffers(buffer, derivations[passwordSize][saltSize][hashName][iterations].slice(0, derivedKeyType.algorithm.length/8)), "Exported key matches correct value");
                                    }, function(err) {
                                        assert_unreached("Exporting derived key failed with error " + err.name + ": " + err.message);
                                    });
                                }, function(err) {
                                    assert_unreached("deriveKey failed with error " + err.name + ": " + err.message);

                                });
                            }, testName);

                            // Test various error conditions for deriveKey:

                            // - illegal name for hash algorithm (NotSupportedError)
                            var badHash = hashName.substring(0, 3) + hashName.substring(4);
                            subsetTest(promise_test, function(test) {
                                return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: badHash, iterations: parseInt(iterations)}, baseKeys[passwordSize], derivedKeyType.algorithm, true, derivedKeyType.usages)
                                .then(function(key) {
                                    assert_unreached("bad hash name should have thrown an NotSupportedError");
                                }, function(err) {
                                    assert_equals(err.name, "NotSupportedError", "deriveKey with bad hash name correctly threw NotSupportedError: " + err.message);
                                });
                            }, testName + " with bad hash name " + badHash);

                            // - baseKey usages missing "deriveKey" (InvalidAccessError)
                            subsetTest(promise_test, function(test) {
                                return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, noKey[passwordSize], derivedKeyType.algorithm, true, derivedKeyType.usages)
                                .then(function(key) {
                                    assert_unreached("missing deriveKey usage should have thrown an InvalidAccessError");
                                }, function(err) {
                                    assert_equals(err.name, "InvalidAccessError", "deriveKey with missing deriveKey usage correctly threw InvalidAccessError: " + err.message);
                                });
                            }, testName + " with missing deriveKey usage");

                            // - baseKey algorithm does not match PBKDF2 (InvalidAccessError)
                            subsetTest(promise_test, function(test) {
                                return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, wrongKey, derivedKeyType.algorithm, true, derivedKeyType.usages)
                                .then(function(key) {
                                    assert_unreached("wrong (ECDH) key should have thrown an InvalidAccessError");
                                }, function(err) {
                                    assert_equals(err.name, "InvalidAccessError", "deriveKey with wrong (ECDH) key correctly threw InvalidAccessError: " + err.message);
                                });
                            }, testName + " with wrong (ECDH) key");

                        });

                        // length not multiple of 8 (OperationError)
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, baseKeys[passwordSize], 44)
                            .then(function(derivation) {
                                assert_unreached("non-multiple of 8 length should have thrown an OperationError");
                            }, function(err) {
                                assert_equals(err.name, "OperationError", "deriveBits with non-multiple of 8 length correctly threw OperationError: " + err.message);
                            });
                        }, testName + " with non-multiple of 8 length");

                        // - illegal name for hash algorithm (NotSupportedError)
                        var badHash = hashName.substring(0, 3) + hashName.substring(4);
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: badHash, iterations: parseInt(iterations)}, baseKeys[passwordSize], 256)
                            .then(function(derivation) {
                                assert_unreached("bad hash name should have thrown an NotSupportedError");
                            }, function(err) {
                                assert_equals(err.name, "NotSupportedError", "deriveBits with bad hash name correctly threw NotSupportedError: " + err.message);
                            });
                        }, testName + " with bad hash name " + badHash);

                        // - baseKey usages missing "deriveBits" (InvalidAccessError)
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, noBits[passwordSize], 256)
                            .then(function(derivation) {
                                assert_unreached("missing deriveBits usage should have thrown an InvalidAccessError");
                            }, function(err) {
                                assert_equals(err.name, "InvalidAccessError", "deriveBits with missing deriveBits usage correctly threw InvalidAccessError: " + err.message);
                            });
                        }, testName + " with missing deriveBits usage");

                        // - baseKey algorithm does not match PBKDF2 (InvalidAccessError)
                        subsetTest(promise_test, function(test) {
                            return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: parseInt(iterations)}, wrongKey, 256)
                            .then(function(derivation) {
                                assert_unreached("wrong (ECDH) key should have thrown an InvalidAccessError");
                            }, function(err) {
                                assert_equals(err.name, "InvalidAccessError", "deriveBits with wrong (ECDH) key correctly threw InvalidAccessError: " + err.message);
                            });
                        }, testName + " with wrong (ECDH) key");
                    });

                    // Check that 0 iterations throws proper error
                    subsetTest(promise_test, function(test) {
                        return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: 0}, baseKeys[passwordSize], 256)
                        .then(function(derivation) {
                            assert_unreached("0 iterations should have thrown an error");
                        }, function(err) {
                            assert_equals(err.name, "OperationError", "deriveBits with 0 iterations correctly threw OperationError: " + err.message);
                        });
                    }, passwordSize + " password, " + saltSize + " salt, " + hashName + ", with 0 iterations");

                    derivedKeyTypes.forEach(function(derivedKeyType) {
                        var testName = "Derived key of type ";
                        Object.keys(derivedKeyType.algorithm).forEach(function(prop) {
                            testName += prop + ": " + derivedKeyType.algorithm[prop] + " ";
                        });
                        testName += " using " + passwordSize + " password, " + saltSize + " salt, " + hashName + ", with 0 iterations";

                        subsetTest(promise_test, function(test) {
                            return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: hashName, iterations: 0}, baseKeys[passwordSize], derivedKeyType.algorithm, true, derivedKeyType.usages)
                            .then(function(derivation) {
                                assert_unreached("0 iterations should have thrown an error");
                            }, function(err) {
                                assert_equals(err.name, "OperationError", "derivekey with 0 iterations correctly threw OperationError: " + err.message);
                            });
                        }, testName);
                    });
                });

                // - legal algorithm name but not digest one (e.g., PBKDF2) (NotSupportedError)
                var nonDigestHash = "PBKDF2";
                [1, 1000, 100000].forEach(function(iterations) {
                    var testName = passwordSize + " password, " + saltSize + " salt, " + nonDigestHash + ", with " + iterations + " iterations";

                    subsetTest(promise_test, function(test) {
                        return subtle.deriveBits({name: "PBKDF2", salt: salts[saltSize], hash: nonDigestHash, iterations: parseInt(iterations)}, baseKeys[passwordSize], 256)
                        .then(function(derivation) {
                            assert_unreached("non-digest algorithm should have thrown an NotSupportedError");
                        }, function(err) {
                            assert_equals(err.name, "NotSupportedError", "deriveBits with non-digest algorithm correctly threw NotSupportedError: " + err.message);
                        });
                    }, testName + " with non-digest algorithm " + nonDigestHash);

                    derivedKeyTypes.forEach(function(derivedKeyType) {
                        var testName = "Derived key of type ";
                        Object.keys(derivedKeyType.algorithm).forEach(function(prop) {
                            testName += prop + ": " + derivedKeyType.algorithm[prop] + " ";
                        });
                        testName += " using " + passwordSize + " password, " + saltSize + " salt, " + nonDigestHash + ", with " + iterations + " iterations";

                        subsetTest(promise_test, function(test) {
                            return subtle.deriveKey({name: "PBKDF2", salt: salts[saltSize], hash: nonDigestHash, iterations: parseInt(iterations)}, baseKeys[passwordSize], derivedKeyType.algorithm, true, derivedKeyType.usages)
                            .then(function(derivation) {
                                assert_unreached("non-digest algorithm should have thrown an NotSupportedError");
                            }, function(err) {
                                assert_equals(err.name, "NotSupportedError", "derivekey with non-digest algorithm correctly threw NotSupportedError: " + err.message);
                            });
                        }, testName);
                    });

                });

            });
        });
    });

    // Deriving bits and keys requires starting with a base key, which is created
    // by importing a password. setUpBaseKeys returns a promise that yields the
    // necessary base keys.
    function setUpBaseKeys(passwords) {
        var promises = [];

        var baseKeys = {};
        var noBits = {};
        var noKey = {};
        var wrongKey = null;

        Object.keys(passwords).forEach(function(passwordSize) {
            var promise = subtle.importKey("raw", passwords[passwordSize], {name: "PBKDF2"}, false, ["deriveKey", "deriveBits"])
            .then(function(baseKey) {
                baseKeys[passwordSize] = baseKey;
            }, function(err) {
                baseKeys[passwordSize] = null;
            });
            promises.push(promise);

            promise = subtle.importKey("raw", passwords[passwordSize], {name: "PBKDF2"}, false, ["deriveBits"])
            .then(function(baseKey) {
                noKey[passwordSize] = baseKey;
            }, function(err) {
                noKey[passwordSize] = null;
            });
            promises.push(promise);

            promise = subtle.importKey("raw", passwords[passwordSize], {name: "PBKDF2"}, false, ["deriveKey"])
            .then(function(baseKey) {
                noBits[passwordSize] = baseKey;
            }, function(err) {
                noBits[passwordSize] = null;
            });
            promises.push(promise);
        });

        var promise = subtle.generateKey({name: "ECDH", namedCurve: "P-256"}, false, ["deriveKey", "deriveBits"])
        .then(function(baseKey) {
            wrongKey = baseKey.privateKey;
        }, function(err) {
            wrongKey = null;
        });
        promises.push(promise);


        return Promise.all(promises).then(function() {
            return {baseKeys: baseKeys, noBits: noBits, noKey: noKey, wrongKey: wrongKey};
        });
    }

    function equalBuffers(a, b) {
        if (a.byteLength !== b.byteLength) {
            return false;
        }

        var aBytes = new Uint8Array(a);
        var bBytes = new Uint8Array(b);

        for (var i=0; i<a.byteLength; i++) {
            if (aBytes[i] !== bBytes[i]) {
                return false;
            }
        }

        return true;
    }

}
