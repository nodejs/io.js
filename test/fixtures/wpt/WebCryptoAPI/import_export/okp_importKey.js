var subtle = crypto.subtle;

function runTests(algorithmName) {
    var algorithm = {name: algorithmName};
    var data = keyData[algorithmName];
    var jwkData = {jwk: {kty: data.jwk.kty, crv: data.jwk.crv, x: data.jwk.x}};

    [true, false].forEach(function(extractable) {
        // Test public keys first
        allValidUsages(data.publicUsages, true).forEach(function(usages) {
            ['spki', 'jwk', 'raw'].forEach(function(format) {
                if (format === "jwk") { // Not all fields used for public keys
                    testFormat(format, algorithm, jwkData, algorithmName, usages, extractable);
                    // Test for https://github.com/WICG/webcrypto-secure-curves/pull/24
                    if (extractable) {
                        testJwkAlgBehaviours(algorithm, jwkData.jwk, algorithmName, usages);
                    }
                } else {
                    testFormat(format, algorithm, data, algorithmName, usages, extractable);
                }
            });

        });

        // Next, test private keys
        allValidUsages(data.privateUsages).forEach(function(usages) {
            ['pkcs8', 'jwk'].forEach(function(format) {
                testFormat(format, algorithm, data, algorithmName, usages, extractable);

                // Test for https://github.com/WICG/webcrypto-secure-curves/pull/24
                if (format === "jwk" && extractable) {
                    testJwkAlgBehaviours(algorithm, data.jwk, algorithmName, usages);
                }
            });
        });
    });
}


// Test importKey with a given key format and other parameters. If
// extrable is true, export the key and verify that it matches the input.
function testFormat(format, algorithm, keyData, keySize, usages, extractable) {
    [algorithm, algorithm.name].forEach((alg) => {
        promise_test(function(test) {
            return subtle.importKey(format, keyData[format], alg, extractable, usages).
                then(function(key) {
                    assert_equals(key.constructor, CryptoKey, "Imported a CryptoKey object");
                    assert_goodCryptoKey(key, algorithm, extractable, usages, (format === 'pkcs8' || (format === 'jwk' && keyData[format].d)) ? 'private' : 'public');
                    if (!extractable) {
                        return;
                    }

                    return subtle.exportKey(format, key).
                        then(function(result) {
                            if (format !== "jwk") {
                                assert_true(equalBuffers(keyData[format], result), "Round trip works");
                            } else {
                                assert_true(equalJwk(keyData[format], result), "Round trip works");
                            }
                        }, function(err) {
                            assert_unreached("Threw an unexpected error: " + err.toString());
                        });
                }, function(err) {
                    assert_unreached("Threw an unexpected error: " + err.toString());
                });
        }, "Good parameters: " + keySize.toString() + " bits " + parameterString(format, keyData[format], alg, extractable, usages));
    });
}

// Test importKey/exportKey "alg" behaviours, alg is ignored upon import and alg is missing for Ed25519 and Ed448 JWK export
// https://github.com/WICG/webcrypto-secure-curves/pull/24
function testJwkAlgBehaviours(algorithm, keyData, crv, usages) {
    [algorithm, algorithm.name].forEach((alg) => {
        promise_test(function(test) {
            return subtle.importKey('jwk', { ...keyData, alg: 'this is ignored' }, alg, true, usages).
                then(function(key) {
                    assert_equals(key.constructor, CryptoKey, "Imported a CryptoKey object");

                    return subtle.exportKey('jwk', key).
                        then(function(result) {
                            assert_equals(Object.keys(result).length, keyData.d ? 6 : 5, "Correct number of JWK members");
                            assert_equals(result.alg, undefined, 'No JWK "alg" member is present');
                            assert_true(equalJwk(keyData, result), "Round trip works");
                        }, function(err) {
                        assert_unreached("Threw an unexpected error: " + err.toString());
                        });
                }, function(err) {
                    assert_unreached("Threw an unexpected error: " + err.toString());
                });
        }, "Good parameters with ignored JWK alg: " + crv.toString() + " " + parameterString('jwk', keyData, alg, true, usages));
    });
}



// Helper methods follow:

// Are two array buffers the same?
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

// Are two Jwk objects "the same"? That is, does the object returned include
// matching values for each property that was expected? It's okay if the
// returned object has extra methods; they aren't checked.
function equalJwk(expected, got) {
    var fields = Object.keys(expected);
    var fieldName;

    for(var i=0; i<fields.length; i++) {
        fieldName = fields[i];
        if (!(fieldName in got)) {
            return false;
        }
        if (expected[fieldName] !== got[fieldName]) {
            return false;
        }
    }

    return true;
}

// Build minimal Jwk objects from raw key data and algorithm specifications
function jwkData(keyData, algorithm) {
    var result = {
        kty: "oct",
        k: byteArrayToUnpaddedBase64(keyData)
    };

    if (algorithm.name.substring(0, 3) === "AES") {
        result.alg = "A" + (8 * keyData.byteLength).toString() + algorithm.name.substring(4);
    } else if (algorithm.name === "HMAC") {
        result.alg = "HS" + algorithm.hash.substring(4);
    }
    return result;
}

// Jwk format wants Base 64 without the typical padding at the end.
function byteArrayToUnpaddedBase64(byteArray){
    var binaryString = "";
    for (var i=0; i<byteArray.byteLength; i++){
        binaryString += String.fromCharCode(byteArray[i]);
    }
    var base64String = btoa(binaryString);

    return base64String.replace(/=/g, "");
}

// Convert method parameters to a string to uniquely name each test
function parameterString(format, data, algorithm, extractable, usages) {
    if ("byteLength" in data) {
        data = "buffer(" + data.byteLength.toString() + ")";
    } else {
        data = "object(" + Object.keys(data).join(", ") + ")";
    }
    var result = "(" +
        objectToString(format) + ", " +
        objectToString(data) + ", " +
        objectToString(algorithm) + ", " +
        objectToString(extractable) + ", " +
        objectToString(usages) +
        ")";

    return result;
}

// Character representation of any object we may use as a parameter.
function objectToString(obj) {
    var keyValuePairs = [];

    if (Array.isArray(obj)) {
        return "[" + obj.map(function(elem){return objectToString(elem);}).join(", ") + "]";
    } else if (typeof obj === "object") {
        Object.keys(obj).sort().forEach(function(keyName) {
            keyValuePairs.push(keyName + ": " + objectToString(obj[keyName]));
        });
        return "{" + keyValuePairs.join(", ") + "}";
    } else if (typeof obj === "undefined") {
        return "undefined";
    } else {
        return obj.toString();
    }

    var keyValuePairs = [];

    Object.keys(obj).sort().forEach(function(keyName) {
        var value = obj[keyName];
        if (typeof value === "object") {
            value = objectToString(value);
        } else if (typeof value === "array") {
            value = "[" + value.map(function(elem){return objectToString(elem);}).join(", ") + "]";
        } else {
            value = value.toString();
        }

        keyValuePairs.push(keyName + ": " + value);
    });

    return "{" + keyValuePairs.join(", ") + "}";
}
