// META: title=WebCryptoAPI: importKey() for EC keys
// META: timeout=long
// META: script=../util/helpers.js

// Test importKey and exportKey for EC algorithms. Only "happy paths" are
// currently tested - those where the operation should succeed.

    var subtle = crypto.subtle;

    var curves = ['P-256', 'P-384', 'P-521'];

    var keyData = {
        "P-521": {
            spki: new Uint8Array([48, 129, 155, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 35, 3, 129, 134, 0, 4, 1, 86, 244, 121, 248, 223, 30, 32, 167, 255, 192, 76, 228, 32, 195, 225, 84, 174, 37, 25, 150, 190, 228, 47, 3, 75, 132, 212, 27, 116, 63, 52, 228, 95, 49, 27, 129, 58, 156, 222, 200, 205, 165, 155, 187, 189, 49, 212, 96, 179, 41, 37, 33, 231, 193, 183, 34, 229, 102, 124, 3, 219, 47, 174, 117, 63, 1, 80, 23, 54, 207, 226, 71, 57, 67, 32, 216, 228, 175, 194, 253, 57, 181, 169, 51, 16, 97, 184, 30, 34, 65, 40, 43, 158, 23, 137, 24, 34, 181, 183, 158, 5, 47, 69, 151, 181, 150, 67, 253, 57, 55, 156, 81, 189, 81, 37, 196, 244, 139, 195, 240, 37, 206, 60, 211, 105, 83, 40, 108, 203, 56, 251]),
            spki_compressed: new Uint8Array([48, 88, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 35, 3, 68, 0, 3, 1, 86, 244, 121, 248, 223, 30, 32, 167, 255, 192, 76, 228, 32, 195, 225, 84, 174, 37, 25, 150, 190, 228, 47, 3, 75, 132, 212, 27, 116, 63, 52, 228, 95, 49, 27, 129, 58, 156, 222, 200, 205, 165, 155, 187, 189, 49, 212, 96, 179, 41, 37, 33, 231, 193, 183, 34, 229, 102, 124, 3, 219, 47, 174, 117, 63]),
            raw: new Uint8Array([4, 1, 86, 244, 121, 248, 223, 30, 32, 167, 255, 192, 76, 228, 32, 195, 225, 84, 174, 37, 25, 150, 190, 228, 47, 3, 75, 132, 212, 27, 116, 63, 52, 228, 95, 49, 27, 129, 58, 156, 222, 200, 205, 165, 155, 187, 189, 49, 212, 96, 179, 41, 37, 33, 231, 193, 183, 34, 229, 102, 124, 3, 219, 47, 174, 117, 63, 1, 80, 23, 54, 207, 226, 71, 57, 67, 32, 216, 228, 175, 194, 253, 57, 181, 169, 51, 16, 97, 184, 30, 34, 65, 40, 43, 158, 23, 137, 24, 34, 181, 183, 158, 5, 47, 69, 151, 181, 150, 67, 253, 57, 55, 156, 81, 189, 81, 37, 196, 244, 139, 195, 240, 37, 206, 60, 211, 105, 83, 40, 108, 203, 56, 251]),
            raw_compressed: new Uint8Array([3, 1, 86, 244, 121, 248, 223, 30, 32, 167, 255, 192, 76, 228, 32, 195, 225, 84, 174, 37, 25, 150, 190, 228, 47, 3, 75, 132, 212, 27, 116, 63, 52, 228, 95, 49, 27, 129, 58, 156, 222, 200, 205, 165, 155, 187, 189, 49, 212, 96, 179, 41, 37, 33, 231, 193, 183, 34, 229, 102, 124, 3, 219, 47, 174, 117, 63]),
            pkcs8: new Uint8Array([48, 129, 238, 2, 1, 0, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 35, 4, 129, 214, 48, 129, 211, 2, 1, 1, 4, 66, 0, 244, 8, 117, 131, 104, 186, 147, 15, 48, 247, 106, 224, 84, 254, 92, 210, 206, 127, 218, 44, 159, 118, 166, 212, 54, 207, 117, 214, 108, 68, 11, 254, 99, 49, 199, 193, 114, 161, 36, 120, 25, 60, 130, 81, 72, 123, 201, 18, 99, 250, 80, 33, 127, 133, 255, 99, 111, 89, 205, 84, 110, 58, 180, 131, 180, 161, 129, 137, 3, 129, 134, 0, 4, 1, 86, 244, 121, 248, 223, 30, 32, 167, 255, 192, 76, 228, 32, 195, 225, 84, 174, 37, 25, 150, 190, 228, 47, 3, 75, 132, 212, 27, 116, 63, 52, 228, 95, 49, 27, 129, 58, 156, 222, 200, 205, 165, 155, 187, 189, 49, 212, 96, 179, 41, 37, 33, 231, 193, 183, 34, 229, 102, 124, 3, 219, 47, 174, 117, 63, 1, 80, 23, 54, 207, 226, 71, 57, 67, 32, 216, 228, 175, 194, 253, 57, 181, 169, 51, 16, 97, 184, 30, 34, 65, 40, 43, 158, 23, 137, 24, 34, 181, 183, 158, 5, 47, 69, 151, 181, 150, 67, 253, 57, 55, 156, 81, 189, 81, 37, 196, 244, 139, 195, 240, 37, 206, 60, 211, 105, 83, 40, 108, 203, 56, 251]),
            jwk: {
                kty: "EC",
                crv: "P-521",
                x: "AVb0efjfHiCn_8BM5CDD4VSuJRmWvuQvA0uE1Bt0PzTkXzEbgTqc3sjNpZu7vTHUYLMpJSHnwbci5WZ8A9svrnU_",
                y: "AVAXNs_iRzlDINjkr8L9ObWpMxBhuB4iQSgrnheJGCK1t54FL0WXtZZD_Tk3nFG9USXE9IvD8CXOPNNpUyhsyzj7",
                d: "APQIdYNoupMPMPdq4FT-XNLOf9osn3am1DbPddZsRAv-YzHHwXKhJHgZPIJRSHvJEmP6UCF_hf9jb1nNVG46tIO0"
            }
        },

        "P-256": {
            spki: new Uint8Array([48, 89, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 3, 66, 0, 4, 210, 16, 176, 166, 249, 217, 240, 18, 134, 128, 88, 180, 63, 164, 244, 113, 1, 133, 67, 187, 160, 12, 146, 80, 223, 146, 87, 194, 172, 174, 93, 209, 206, 3, 117, 82, 212, 129, 69, 12, 227, 155, 77, 16, 149, 112, 27, 23, 91, 250, 179, 75, 142, 108, 9, 158, 24, 241, 193, 152, 53, 131, 97, 232]),
            spki_compressed: new Uint8Array([48, 57, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 3, 34, 0, 2, 210, 16, 176, 166, 249, 217, 240, 18, 134, 128, 88, 180, 63, 164, 244, 113, 1, 133, 67, 187, 160, 12, 146, 80, 223, 146, 87, 194, 172, 174, 93, 209]),
            raw: new Uint8Array([4, 210, 16, 176, 166, 249, 217, 240, 18, 134, 128, 88, 180, 63, 164, 244, 113, 1, 133, 67, 187, 160, 12, 146, 80, 223, 146, 87, 194, 172, 174, 93, 209, 206, 3, 117, 82, 212, 129, 69, 12, 227, 155, 77, 16, 149, 112, 27, 23, 91, 250, 179, 75, 142, 108, 9, 158, 24, 241, 193, 152, 53, 131, 97, 232]),
            raw_compressed: new Uint8Array([2, 210, 16, 176, 166, 249, 217, 240, 18, 134, 128, 88, 180, 63, 164, 244, 113, 1, 133, 67, 187, 160, 12, 146, 80, 223, 146, 87, 194, 172, 174, 93, 209]),
            pkcs8: new Uint8Array([48, 129, 135, 2, 1, 0, 48, 19, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 8, 42, 134, 72, 206, 61, 3, 1, 7, 4, 109, 48, 107, 2, 1, 1, 4, 32, 19, 211, 58, 45, 90, 191, 156, 249, 235, 178, 31, 248, 96, 212, 174, 254, 110, 86, 231, 119, 144, 244, 222, 233, 180, 8, 132, 235, 211, 53, 68, 234, 161, 68, 3, 66, 0, 4, 210, 16, 176, 166, 249, 217, 240, 18, 134, 128, 88, 180, 63, 164, 244, 113, 1, 133, 67, 187, 160, 12, 146, 80, 223, 146, 87, 194, 172, 174, 93, 209, 206, 3, 117, 82, 212, 129, 69, 12, 227, 155, 77, 16, 149, 112, 27, 23, 91, 250, 179, 75, 142, 108, 9, 158, 24, 241, 193, 152, 53, 131, 97, 232]),
            jwk: {
                kty: "EC",
                crv: "P-256",
                x: "0hCwpvnZ8BKGgFi0P6T0cQGFQ7ugDJJQ35JXwqyuXdE",
                y: "zgN1UtSBRQzjm00QlXAbF1v6s0uObAmeGPHBmDWDYeg",
                d: "E9M6LVq_nPnrsh_4YNSu_m5W53eQ9N7ptAiE69M1ROo"
            }
        },

        "P-384": {
            spki: new Uint8Array([48, 118, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 34, 3, 98, 0, 4, 33, 156, 20, 214, 102, 23, 179, 110, 198, 216, 133, 107, 56, 91, 115, 167, 77, 52, 79, 216, 174, 117, 239, 4, 100, 53, 221, 165, 78, 59, 68, 189, 95, 189, 235, 209, 208, 141, 214, 158, 45, 125, 193, 220, 33, 140, 180, 53, 189, 40, 19, 140, 199, 120, 51, 122, 132, 47, 107, 214, 27, 36, 14, 116, 36, 159, 36, 102, 124, 42, 88, 16, 167, 107, 252, 40, 224, 51, 95, 136, 166, 80, 29, 236, 1, 151, 109, 168, 90, 251, 0, 134, 156, 182, 172, 232]),
            spki_compressed: new Uint8Array([48, 70, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 34, 3, 50, 0, 2, 33, 156, 20, 214, 102, 23, 179, 110, 198, 216, 133, 107, 56, 91, 115, 167, 77, 52, 79, 216, 174, 117, 239, 4, 100, 53, 221, 165, 78, 59, 68, 189, 95, 189, 235, 209, 208, 141, 214, 158, 45, 125, 193, 220, 33, 140, 180, 53]),
            raw: new Uint8Array([4, 33, 156, 20, 214, 102, 23, 179, 110, 198, 216, 133, 107, 56, 91, 115, 167, 77, 52, 79, 216, 174, 117, 239, 4, 100, 53, 221, 165, 78, 59, 68, 189, 95, 189, 235, 209, 208, 141, 214, 158, 45, 125, 193, 220, 33, 140, 180, 53, 189, 40, 19, 140, 199, 120, 51, 122, 132, 47, 107, 214, 27, 36, 14, 116, 36, 159, 36, 102, 124, 42, 88, 16, 167, 107, 252, 40, 224, 51, 95, 136, 166, 80, 29, 236, 1, 151, 109, 168, 90, 251, 0, 134, 156, 182, 172, 232]),
            raw_compressed: new Uint8Array([2, 33, 156, 20, 214, 102, 23, 179, 110, 198, 216, 133, 107, 56, 91, 115, 167, 77, 52, 79, 216, 174, 117, 239, 4, 100, 53, 221, 165, 78, 59, 68, 189, 95, 189, 235, 209, 208, 141, 214, 158, 45, 125, 193, 220, 33, 140, 180, 53]),
            pkcs8: new Uint8Array([48, 129, 182, 2, 1, 0, 48, 16, 6, 7, 42, 134, 72, 206, 61, 2, 1, 6, 5, 43, 129, 4, 0, 34, 4, 129, 158, 48, 129, 155, 2, 1, 1, 4, 48, 69, 55, 181, 153, 7, 132, 211, 194, 210, 46, 150, 168, 249, 47, 161, 170, 73, 46, 232, 115, 229, 118, 164, 21, 130, 225, 68, 24, 60, 152, 136, 209, 14, 107, 158, 180, 206, 212, 178, 204, 64, 18, 228, 172, 94, 168, 64, 115, 161, 100, 3, 98, 0, 4, 33, 156, 20, 214, 102, 23, 179, 110, 198, 216, 133, 107, 56, 91, 115, 167, 77, 52, 79, 216, 174, 117, 239, 4, 100, 53, 221, 165, 78, 59, 68, 189, 95, 189, 235, 209, 208, 141, 214, 158, 45, 125, 193, 220, 33, 140, 180, 53, 189, 40, 19, 140, 199, 120, 51, 122, 132, 47, 107, 214, 27, 36, 14, 116, 36, 159, 36, 102, 124, 42, 88, 16, 167, 107, 252, 40, 224, 51, 95, 136, 166, 80, 29, 236, 1, 151, 109, 168, 90, 251, 0, 134, 156, 182, 172, 232]),
            jwk: {
                kty: "EC",
                crv: "P-384",
                x: "IZwU1mYXs27G2IVrOFtzp000T9iude8EZDXdpU47RL1fvevR0I3Wni19wdwhjLQ1",
                y: "vSgTjMd4M3qEL2vWGyQOdCSfJGZ8KlgQp2v8KOAzX4imUB3sAZdtqFr7AIactqzo",
                d: "RTe1mQeE08LSLpao-S-hqkku6HPldqQVguFEGDyYiNEOa560ztSyzEAS5KxeqEBz"
            }
        },

    };

    // combinations to test
    var testVectors = [
        {name: "ECDSA", privateUsages: ["sign"], publicUsages: ["verify"]},
        {name: "ECDH",  privateUsages: ["deriveKey", "deriveBits"], publicUsages: []}
    ];

    // TESTS ARE HERE:
    // Test every test vector, along with all available key data
    testVectors.forEach(function(vector) {
        curves.forEach(function(curve) {

            [true, false].forEach(function(extractable) {

                // Test public keys first
                allValidUsages(vector.publicUsages, true).forEach(function(usages) {
                    ['spki', 'spki_compressed', 'jwk', 'raw', 'raw_compressed'].forEach(function(format) {
                        var algorithm = {name: vector.name, namedCurve: curve};
                        var data = keyData[curve];
                        if (format === "jwk") { // Not all fields used for public keys
                            data = {jwk: {kty: keyData[curve].jwk.kty, crv: keyData[curve].jwk.crv, x: keyData[curve].jwk.x, y: keyData[curve].jwk.y}};
                        }

                        testFormat(format, algorithm, data, curve, usages, extractable);
                        if (vector.name === 'ECDH' && format === 'jwk') {
                            testEcdhJwkAlg(algorithm, { ...data.jwk, alg: 'any alg works here' }, curve, usages, extractable);
                        }
                    });

                });

                // Next, test private keys
                ['pkcs8', 'jwk'].forEach(function(format) {
                    var algorithm = {name: vector.name, namedCurve: curve};
                    var data = keyData[curve];
                    allValidUsages(vector.privateUsages).forEach(function(usages) {
                        testFormat(format, algorithm, data, curve, usages, extractable);
                        if (vector.name === 'ECDH' && format === 'jwk') {
                            testEcdhJwkAlg(algorithm, { ...data.jwk, alg: 'any alg works here' }, curve, usages, extractable);
                        }
                    });
                    testEmptyUsages(format, algorithm, data, curve, extractable);
                });
            });
        });
    });


    // Test importKey with a given key format and other parameters. If
    // extrable is true, export the key and verify that it matches the input.
    function testFormat(format, algorithm, data, keySize, usages, extractable) {
        const keyData = data[format];
        const compressed = format.endsWith("_compressed");
        if (compressed) {
            [format] = format.split("_compressed");
        }
        promise_test(function(test) {
            return subtle.importKey(format, keyData, algorithm, extractable, usages).
            then(function(key) {
                assert_equals(key.constructor, CryptoKey, "Imported a CryptoKey object");
                assert_goodCryptoKey(key, algorithm, extractable, usages, (format === 'pkcs8' || (format === 'jwk' && keyData.d)) ? 'private' : 'public');
                if (!extractable) {
                    return;
                }

                return subtle.exportKey(format, key).
                then(function(result) {
                    if (format !== "jwk") {
                        assert_true(equalBuffers(data[format], result), "Round trip works");
                    } else {
                        assert_true(equalJwk(data[format], result), "Round trip works");
                    }
                }, function(err) {
                    assert_unreached("Threw an unexpected error: " + err.toString());
                });
            }, function(err) {
                if (compressed && err.name === "DataError") {
                    assert_implements_optional(false, "Compressed point format not supported: " + err.toString());
                } else {
                    assert_unreached("Threw an unexpected error: " + err.toString());
                }
            });
        }, "Good parameters: " + keySize.toString() + " bits " + parameterString(format, compressed, keyData, algorithm, extractable, usages));
    }

    // Test importKey with a given key format and other parameters but with empty usages.
    // Should fail with SyntaxError
    function testEmptyUsages(format, algorithm, data, keySize, extractable) {
        const keyData = data[format];
        const usages = [];
        promise_test(function(test) {
            return subtle.importKey(format, keyData, algorithm, extractable, usages).
            then(function(key) {
                assert_unreached("importKey succeeded but should have failed with SyntaxError");
            }, function(err) {
                assert_equals(err.name, "SyntaxError", "Should throw correct error, not " + err.name + ": " + err.message);
            });
        }, "Empty Usages: " + keySize.toString() + " bits " + parameterString(format, false, keyData, algorithm, extractable, usages));
    }

    // Test ECDH importKey with a JWK format
    // Should succeed with any "alg" value
    function testEcdhJwkAlg(algorithm, keyData, keySize, usages, extractable) {
        const format = "jwk";
        promise_test(function(test) {
            return subtle.importKey(format, keyData, algorithm, extractable, usages).
            then(function(key) {
                assert_equals(key.constructor, CryptoKey, "Imported a CryptoKey object");
                assert_goodCryptoKey(key, algorithm, extractable, usages, keyData.d ? 'private' : 'public');
            }, function(err) {
                assert_unreached("Threw an unexpected error: " + err.toString());
            });
        }, "ECDH any JWK alg: " + keySize.toString() + " bits " + parameterString(format, false, keyData, algorithm, extractable, usages));
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
    function parameterString(format, compressed, data, algorithm, extractable, usages) {
        if ("byteLength" in data) {
            data = "buffer(" + data.byteLength.toString() + (compressed ? ", compressed" : "") + ")";
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
