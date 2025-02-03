
function define_tests() {
    // May want to test prefixed implementations.
    var subtle = self.crypto.subtle;

    var pkcs8 = {
        "P-521": new Uint8Array([48, 129, 238, 2, 1, 0, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 35, 4, 129, 214, 48, 129, 211, 2, 1, 1, 4, 66, 1, 166, 126, 211, 33, 145, 90, 100, 170, 53, 155, 125, 100, 141, 220, 38, 24, 250, 142, 141, 24, 103, 232, 247, 24, 48, 177, 13, 37, 237, 40, 145, 250, 241, 47, 60, 126, 117, 66, 26, 46, 162, 100, 249, 169, 21, 50, 13, 39, 79, 225, 71, 7, 66, 185, 132, 233, 107, 152, 145, 32, 129, 250, 205, 71, 141, 161, 129, 137, 3, 129, 134, 0, 4, 0, 32, 157, 72, 63, 40, 102, 104, 129, 198, 100, 31, 58, 18, 111, 64, 15, 81, 228, 101, 17, 112, 254, 103, 140, 117, 232, 87, 18, 226, 134, 138, 220, 133, 8, 36, 153, 123, 235, 240, 188, 130, 180, 48, 40, 166, 210, 236, 23, 119, 202, 69, 39, 159, 114, 6, 163, 234, 139, 92, 210, 7, 63, 73, 62, 69, 0, 12, 181, 76, 58, 90, 202, 162, 104, 197, 103, 16, 66, 136, 120, 217, 139, 138, 251, 246, 138, 97, 33, 83, 99, 40, 70, 216, 7, 233, 38, 114, 105, 143, 27, 156, 97, 29, 231, 211, 142, 52, 205, 108, 115, 136, 144, 146, 197, 110, 82, 214, 128, 241, 223, 208, 146, 184, 122, 200, 239, 159, 243, 200, 251, 72]),
        "P-256": new Uint8Array([48, 129, 135, 2, 1, 0, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 4, 109, 48, 107, 2, 1, 1, 4, 32, 15, 247, 79, 232, 241, 202, 175, 97, 92, 206, 241, 29, 217, 53, 114, 87, 98, 217, 216, 65, 236, 186, 185, 94, 170, 38, 68, 123, 52, 100, 245, 113, 161, 68, 3, 66, 0, 4, 140, 96, 11, 44, 102, 25, 45, 97, 158, 39, 210, 37, 107, 59, 151, 118, 178, 141, 30, 5, 246, 13, 234, 189, 98, 174, 123, 154, 211, 157, 224, 217, 59, 4, 102, 109, 199, 119, 14, 126, 207, 13, 211, 203, 203, 211, 110, 221, 107, 94, 220, 153, 81, 7, 55, 161, 237, 104, 46, 205, 112, 244, 10, 47]),
        "P-384": new Uint8Array([48, 129, 182, 2, 1, 0, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 34, 4, 129, 158, 48, 129, 155, 2, 1, 1, 4, 48, 248, 113, 165, 102, 101, 137, 193, 74, 87, 71, 38, 62, 248, 91, 49, 156, 192, 35, 219, 110, 53, 103, 108, 61, 120, 30, 239, 139, 5, 95, 207, 190, 134, 250, 13, 6, 208, 86, 181, 25, 95, 177, 50, 58, 248, 222, 37, 179, 161, 100, 3, 98, 0, 4, 241, 25, 101, 223, 125, 212, 89, 77, 4, 25, 197, 8, 100, 130, 163, 184, 38, 185, 121, 127, 155, 224, 189, 13, 16, 156, 158, 30, 153, 137, 193, 185, 169, 43, 143, 38, 159, 152, 225, 122, 209, 132, 186, 115, 193, 247, 151, 98, 175, 69, 175, 129, 65, 96, 38, 66, 218, 39, 26, 107, 176, 255, 235, 12, 180, 71, 143, 207, 112, 126, 102, 26, 166, 214, 205, 245, 21, 73, 200, 140, 63, 19, 11, 233, 232, 32, 31, 111, 106, 9, 244, 24, 90, 175, 149, 196])
    };

    var spki = {
        "P-521": new Uint8Array([48, 129, 155, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 35, 3, 129, 134, 0, 4, 0, 238, 105, 249, 71, 21, 215, 1, 233, 226, 1, 19, 51, 212, 244, 249, 108, 186, 125, 145, 248, 139, 17, 43, 175, 117, 207, 9, 204, 31, 138, 202, 151, 97, 141, 169, 56, 152, 34, 210, 155, 111, 233, 153, 106, 97, 32, 62, 247, 82, 183, 113, 232, 149, 143, 196, 103, 123, 179, 119, 133, 101, 171, 96, 214, 237, 0, 222, 171, 103, 97, 137, 91, 147, 94, 58, 211, 37, 251, 133, 73, 229, 111, 19, 120, 106, 167, 63, 136, 162, 236, 254, 64, 147, 52, 115, 216, 174, 242, 64, 196, 223, 215, 213, 6, 242, 44, 221, 14, 85, 85, 143, 63, 191, 5, 235, 247, 239, 239, 122, 114, 215, 143, 70, 70, 155, 132, 72, 242, 110, 39, 18]),
        "P-256": new Uint8Array([48, 89, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 3, 66, 0, 4, 154, 116, 32, 120, 126, 95, 77, 105, 211, 232, 34, 114, 115, 1, 109, 56, 224, 71, 129, 133, 223, 127, 238, 156, 142, 103, 60, 202, 211, 79, 126, 128, 254, 49, 141, 182, 221, 107, 119, 218, 99, 32, 165, 246, 151, 89, 9, 68, 23, 177, 52, 239, 138, 139, 116, 193, 101, 4, 57, 198, 115, 0, 90, 61]),
        "P-384": new Uint8Array([48, 118, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 34, 3, 98, 0, 4, 145, 130, 45, 194, 175, 89, 193, 143, 91, 103, 248, 13, 246, 26, 38, 3, 194, 168, 240, 179, 192, 175, 130, 45, 99, 194, 121, 112, 26, 130, 69, 96, 64, 68, 1, 221, 233, 165, 110, 229, 39, 87, 234, 139, 199, 72, 212, 200, 43, 83, 55, 180, 141, 123, 101, 88, 58, 61, 87, 36, 56, 136, 0, 54, 186, 198, 115, 15, 66, 202, 82, 120, 150, 107, 213, 242, 30, 134, 226, 29, 48, 197, 166, 208, 70, 62, 197, 19, 221, 80, 159, 252, 220, 175, 31, 245])
    };

    var sizes = {
        "P-521": 66,
        "P-256": 32,
        "P-384": 48
    };

    var derivations = {
        "P-521": new Uint8Array([0, 156, 43, 206, 87, 190, 128, 173, 171, 59, 7, 56, 91, 142, 89, 144, 235, 125, 111, 222, 189, 176, 27, 243, 83, 113, 164, 246, 7, 94, 157, 40, 138, 193, 42, 109, 254, 3, 170, 87, 67, 188, 129, 112, 157, 73, 168, 34, 148, 2, 25, 182, 75, 118, 138, 205, 82, 15, 161, 54, 142, 160, 175, 141, 71, 93]),
        "P-256": new Uint8Array([14, 143, 60, 77, 177, 178, 162, 131, 115, 90, 0, 220, 87, 31, 26, 232, 151, 28, 227, 35, 250, 17, 131, 137, 203, 95, 65, 196, 59, 61, 181, 161]),
        "P-384": new Uint8Array([224, 189, 107, 206, 10, 239, 140, 164, 136, 56, 166, 226, 252, 197, 126, 103, 185, 197, 232, 134, 12, 95, 11, 233, 218, 190, 197, 62, 69, 78, 24, 160, 161, 116, 196, 136, 136, 162, 100, 136, 17, 91, 45, 201, 241, 223, 165, 45])
    };

    return importKeys(pkcs8, spki, sizes)
    .then(function(results) {
        publicKeys = results.publicKeys;
        privateKeys = results.privateKeys;
        ecdsaKeyPairs = results.ecdsaKeyPairs;
        noDeriveBitsKeys = results.noDeriveBitsKeys;

        Object.keys(sizes).forEach(function(namedCurve) {
            // Basic success case
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_true(equalBuffers(derivation, derivations[namedCurve]), "Derived correct bits");
                }, function(err) {
                    assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                });
            }, namedCurve + " good parameters");

            // Case insensitivity check
            promise_test(function(test) {
                return subtle.deriveBits({name: "EcDh", public: publicKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_true(equalBuffers(derivation, derivations[namedCurve]), "Derived correct bits");
                }, function(err) {
                    assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                });
            }, namedCurve + " mixed case parameters");

            // Shorter than entire derivation per algorithm
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve] - 32)
                .then(function(derivation) {
                    assert_true(equalBuffers(derivation, derivations[namedCurve], 8 * sizes[namedCurve] - 32), "Derived correct bits");
                }, function(err) {
                    assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                });
            }, namedCurve + " short result");

            // Non-multiple of 8
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve] - 11)
                .then(function(derivation) {
                    assert_true(equalBuffers(derivation, derivations[namedCurve], 8 * sizes[namedCurve] - 11), "Derived correct bits");
                }, function(err) {
                    assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
                });
            }, namedCurve + " non-multiple of 8 bits");

            // Errors to test:

            // - missing public property TypeError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH"}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with TypeError");
                }, function(err) {
                    assert_equals(err.name, "TypeError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " missing public curve");

            // - Non CryptoKey public property TypeError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: {message: "Not a CryptoKey"}}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with TypeError");
                }, function(err) {
                    assert_equals(err.name, "TypeError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " public property of algorithm is not a CryptoKey");

            // - wrong named curve
            promise_test(function(test) {
                publicKey = publicKeys["P-256"];
                if (namedCurve === "P-256") {
                    publicKey = publicKeys["P-384"];
                }
                return subtle.deriveBits({name: "ECDH", public: publicKey}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                }, function(err) {
                    assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " mismatched curves");

            // - not ECDH public property InvalidAccessError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: ecdsaKeyPairs[namedCurve].publicKey}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                }, function(err) {
                    assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " public property of algorithm is not an ECDSA public key");

            // - No deriveBits usage in baseKey InvalidAccessError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, noDeriveBitsKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                }, function(err) {
                    assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " no deriveBits usage for base key");

            // - Use public key for baseKey InvalidAccessError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, publicKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                }, function(err) {
                    assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " base key is not a private key");

            // - Use private key for public property InvalidAccessError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: privateKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                }, function(err) {
                    assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " public property value is a private key");

            // - Use secret key for public property InvalidAccessError
            promise_test(function(test) {
                return subtle.generateKey({name: "AES-CBC", length: 128}, true, ["encrypt", "decrypt"])
                .then(function(secretKey) {
                    return subtle.deriveBits({name: "ECDH", public: secretKey}, privateKeys[namedCurve], 8 * sizes[namedCurve])
                    .then(function(derivation) {
                        assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                    }, function(err) {
                        assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                    });
                });
            }, namedCurve + " public property value is a secret key");

            // - Length greater than 256, 384, 521 for particular curves OperationError
            promise_test(function(test) {
                return subtle.deriveBits({name: "ECDH", public: publicKeys[namedCurve]}, privateKeys[namedCurve], 8 * sizes[namedCurve] + 8)
                .then(function(derivation) {
                    assert_unreached("deriveBits succeeded but should have failed with OperationError");
                }, function(err) {
                    assert_equals(err.name, "OperationError", "Should throw correct error, not " + err.name + ": " + err.message);
                });
            }, namedCurve + " asking for too many bits");
        });
    });

    function importKeys(pkcs8, spki, sizes) {
        var privateKeys = {};
        var publicKeys = {};
        var ecdsaKeyPairs = {};
        var noDeriveBitsKeys = {};

        var promises = [];
        Object.keys(pkcs8).forEach(function(namedCurve) {
            var operation = subtle.importKey("pkcs8", pkcs8[namedCurve],
                                            {name: "ECDH", namedCurve: namedCurve},
                                            false, ["deriveBits", "deriveKey"])
                            .then(function(key) {
                                privateKeys[namedCurve] = key;
                            }, function (err) {
                                privateKeys[namedCurve] = null;
                            });
            promises.push(operation);
        });
        Object.keys(pkcs8).forEach(function(namedCurve) {
            var operation = subtle.importKey("pkcs8", pkcs8[namedCurve],
                                            {name: "ECDH", namedCurve: namedCurve},
                                            false, ["deriveKey"])
                            .then(function(key) {
                                noDeriveBitsKeys[namedCurve] = key;
                            }, function (err) {
                                noDeriveBitsKeys[namedCurve] = null;
                            });
            promises.push(operation);
        });
        Object.keys(spki).forEach(function(namedCurve) {
            var operation = subtle.importKey("spki", spki[namedCurve],
                                            {name: "ECDH", namedCurve: namedCurve},
                                            false, [])
                            .then(function(key) {
                                publicKeys[namedCurve] = key;
                            }, function (err) {
                                publicKeys[namedCurve] = null;
                            });
            promises.push(operation);
        });
        Object.keys(sizes).forEach(function(namedCurve) {
            var operation = subtle.generateKey({name: "ECDSA", namedCurve: namedCurve}, false, ["sign", "verify"])
                            .then(function(keyPair) {
                                ecdsaKeyPairs[namedCurve] = keyPair;
                            }, function (err) {
                                ecdsaKeyPairs[namedCurve] = null;
                            });
            promises.push(operation);
        });

        return Promise.all(promises)
               .then(function(results) {return {privateKeys: privateKeys, publicKeys: publicKeys, ecdsaKeyPairs: ecdsaKeyPairs, noDeriveBitsKeys: noDeriveBitsKeys}});
    }

    // Compares two ArrayBuffer or ArrayBufferView objects. If bitCount is
    // omitted, the two values must be the same length and have the same contents
    // in every byte. If bitCount is included, only that leading number of bits
    // have to match.
    function equalBuffers(a, b, bitCount) {
        var remainder;

        if (typeof bitCount === "undefined" && a.byteLength !== b.byteLength) {
            return false;
        }

        var aBytes = new Uint8Array(a);
        var bBytes = new Uint8Array(b);

        var length = a.byteLength;
        if (typeof bitCount !== "undefined") {
            length = Math.floor(bitCount / 8);
        }

        for (var i=0; i<length; i++) {
            if (aBytes[i] !== bBytes[i]) {
                return false;
            }
        }

        if (typeof bitCount !== "undefined") {
            remainder = bitCount % 8;
            return aBytes[length] >> (8 - remainder) === bBytes[length] >> (8 - remainder);
        }

        return true;
    }

}
