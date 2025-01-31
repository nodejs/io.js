// META: title=WebCryptoAPI: digest()
// META: timeout=long

    var subtle = crypto.subtle; // Change to test prefixed implementations

    var sourceData = {
        empty: new Uint8Array(0),
        short: new Uint8Array([21, 110, 234, 124, 193, 76, 86, 203, 148, 219, 3, 10, 74, 157, 149, 255]),
        medium: new Uint8Array([182, 200, 249, 223, 100, 140, 208, 136, 183, 15, 56, 231, 65, 151, 177, 140, 184, 30, 30, 67, 80, 213, 11, 204, 184, 251, 90, 115, 121, 200, 123, 178, 227, 214, 237, 84, 97, 237, 30, 159, 54, 243, 64, 163, 150, 42, 68, 107, 129, 91, 121, 75, 75, 212, 58, 68, 3, 80, 32, 119, 178, 37, 108, 200, 7, 131, 127, 58, 172, 209, 24, 235, 75, 156, 43, 174, 184, 151, 6, 134, 37, 171, 172, 161, 147])
    };

    sourceData.long = new Uint8Array(1024 * sourceData.medium.byteLength);
    for (var i=0; i<1024; i++) {
        sourceData.long.set(sourceData.medium, i * sourceData.medium.byteLength);
    }

    var digestedData = {
        "sha-1": {
            empty: new Uint8Array([218, 57, 163, 238, 94, 107, 75, 13, 50, 85, 191, 239, 149, 96, 24, 144, 175, 216, 7, 9]),
            short: new Uint8Array([201, 19, 24, 205, 242, 57, 106, 1, 94, 63, 78, 106, 134, 160, 186, 101, 184, 99, 89, 68]),
            medium: new Uint8Array([229, 65, 6, 8, 112, 235, 22, 191, 51, 182, 142, 81, 245, 19, 82, 104, 147, 152, 103, 41]),
            long: new Uint8Array([48, 152, 181, 0, 55, 236, 208, 46, 189, 101, 118, 83, 178, 191, 160, 30, 238, 39, 162, 234])
        },
        "sha-256": {
            empty: new Uint8Array([227, 176, 196, 66, 152, 252, 28, 20, 154, 251, 244, 200, 153, 111, 185, 36, 39, 174, 65, 228, 100, 155, 147, 76, 164, 149, 153, 27, 120, 82, 184, 85]),
            short: new Uint8Array([162, 131, 17, 134, 152, 71, 146, 199, 211, 45, 89, 200, 151, 64, 104, 127, 25, 173, 220, 27, 149, 158, 113, 161, 204, 83, 138, 59, 126, 216, 67, 242]),
            medium: new Uint8Array([83, 83, 103, 135, 126, 240, 20, 215, 252, 113, 126, 92, 183, 132, 62, 89, 182, 26, 238, 98, 199, 2, 156, 236, 126, 198, 193, 47, 217, 36, 224, 228]),
            long: new Uint8Array([20, 205, 234, 157, 199, 95, 90, 98, 116, 217, 252, 30, 100, 0, 153, 18, 241, 220, 211, 6, 180, 143, 232, 233, 207, 18, 45, 230, 113, 87, 23, 129])
        },
        "sha-384": {
            empty: new Uint8Array([56, 176, 96, 167, 81, 172, 150, 56, 76, 217, 50, 126, 177, 177, 227, 106, 33, 253, 183, 17, 20, 190, 7, 67, 76, 12, 199, 191, 99, 246, 225, 218, 39, 78, 222, 191, 231, 111, 101, 251, 213, 26, 210, 241, 72, 152, 185, 91]),
            short: new Uint8Array([107, 245, 234, 101, 36, 209, 205, 220, 67, 247, 207, 59, 86, 238, 5, 146, 39, 64, 74, 47, 83, 143, 2, 42, 61, 183, 68, 122, 120, 44, 6, 193, 237, 5, 232, 171, 79, 94, 220, 23, 243, 113, 20, 64, 223, 233, 119, 49]),
            medium: new Uint8Array([203, 194, 197, 136, 254, 91, 37, 249, 22, 218, 40, 180, 228, 122, 72, 74, 230, 252, 31, 228, 144, 45, 213, 201, 147, 154, 107, 253, 3, 74, 179, 180, 139, 57, 8, 116, 54, 1, 31, 106, 153, 135, 157, 39, 149, 64, 233, 119]),
            long: new Uint8Array([73, 244, 253, 179, 152, 25, 104, 249, 125, 87, 55, 15, 133, 52, 80, 103, 205, 82, 150, 169, 125, 209, 161, 142, 6, 145, 30, 117, 110, 150, 8, 73, 37, 41, 135, 14, 26, 209, 48, 153, 141, 87, 203, 251, 183, 193, 208, 158])
        },
        "sha-512": {
            empty: new Uint8Array([207, 131, 225, 53, 126, 239, 184, 189, 241, 84, 40, 80, 214, 109, 128, 7, 214, 32, 228, 5, 11, 87, 21, 220, 131, 244, 169, 33, 211, 108, 233, 206, 71, 208, 209, 60, 93, 133, 242, 176, 255, 131, 24, 210, 135, 126, 236, 47, 99, 185, 49, 189, 71, 65, 122, 129, 165, 56, 50, 122, 249, 39, 218, 62]),
            short: new Uint8Array([55, 82, 72, 190, 95, 243, 75, 231, 76, 171, 79, 241, 195, 188, 141, 198, 139, 213, 248, 223, 244, 2, 62, 152, 248, 123, 134, 92, 255, 44, 114, 66, 146, 223, 24, 148, 67, 166, 79, 244, 19, 74, 101, 205, 70, 53, 185, 212, 245, 220, 13, 63, 182, 117, 40, 0, 42, 99, 172, 242, 108, 157, 165, 117]),
            medium: new Uint8Array([185, 16, 159, 131, 158, 142, 164, 60, 137, 15, 41, 60, 225, 29, 198, 226, 121, 141, 30, 36, 49, 241, 228, 185, 25, 227, 178, 12, 79, 54, 48, 59, 163, 156, 145, 109, 179, 6, 196, 90, 59, 101, 118, 31, 245, 190, 133, 50, 142, 234, 244, 44, 56, 48, 241, 217, 94, 122, 65, 22, 91, 125, 45, 54]),
            long: new Uint8Array([75, 2, 202, 246, 80, 39, 96, 48, 234, 86, 23, 229, 151, 197, 213, 63, 217, 218, 166, 139, 120, 191, 230, 11, 34, 170, 184, 211, 106, 76, 42, 58, 255, 219, 113, 35, 79, 73, 39, 103, 55, 197, 117, 221, 247, 77, 20, 5, 76, 189, 111, 219, 152, 253, 13, 220, 188, 180, 111, 145, 173, 118, 182, 238])
        },
    }

    // Try every combination of hash with source data size. Variations tested are
    // hash name in upper, lower, or mixed case, and upper-case version with the
    // source buffer altered after call.
    Object.keys(sourceData).forEach(function(size) {
        Object.keys(digestedData).forEach(function(alg) {
            var upCase = alg.toUpperCase();
            var downCase = alg.toLowerCase();
            var mixedCase = upCase.substr(0, 1) + downCase.substr(1);

            promise_test(function(test) {
                var promise = subtle.digest({name: upCase}, sourceData[size])
                .then(function(result) {
                    assert_true(equalBuffers(result, digestedData[alg][size]), "digest() yielded expected result for " + alg + ":" + size);
                }, function(err) {
                    assert_unreached("digest() threw an error for " + alg + ":" + size + " - " + err.message);
                });

                return promise;
            }, upCase + " with " + size + " source data");

            promise_test(function(test) {
                var promise = subtle.digest({name: downCase}, sourceData[size])
                .then(function(result) {
                    assert_true(equalBuffers(result, digestedData[alg][size]), "digest() yielded expected result for " + alg + ":" + size);
                }, function(err) {
                    assert_unreached("digest() threw an error for " + alg + ":" + size + " - " + err.message);
                });

                return promise;
            }, downCase + " with " + size + " source data");

            promise_test(function(test) {
                var promise = subtle.digest({name: mixedCase}, sourceData[size])
                .then(function(result) {
                    assert_true(equalBuffers(result, digestedData[alg][size]), "digest() yielded expected result for " + alg + ":" + size);
                }, function(err) {
                    assert_unreached("digest() threw an error for " + alg + ":" + size + " - " + err.message);
                });

                return promise;
            }, mixedCase + " with " + size + " source data");

            promise_test(function(test) {
                var copiedBuffer = copyBuffer(sourceData[size]);
                var promise = subtle.digest({name: upCase}, copiedBuffer)
                .then(function(result) {
                    assert_true(equalBuffers(result, digestedData[alg][size]), "digest() yielded expected result for " + alg + ":" + size);
                }, function(err) {
                    assert_unreached("digest() threw an error for " + alg + ":" + size + " - " + err.message);
                });

                copiedBuffer[0] = 255 - copiedBuffer;
                return promise;
            }, upCase + " with " + size + " source data and altered buffer after call");

        });
    });

    // Call digest() with bad algorithm names to get an error
    var badNames = ["AES-GCM", "RSA-OAEP", "PBKDF2", "AES-KW"];
    Object.keys(sourceData).forEach(function(size) {
        badNames.forEach(function(badName) {

            promise_test(function(test) {
                var promise = subtle.digest({name: badName}, sourceData[size])
                .then(function(result) {
                    assert_unreached("digest() should not have worked for " + badName + ":" + size);
                }, function(err) {
                    assert_equals(err.name, "NotSupportedError", "Bad algorithm name should cause NotSupportedError")
                });

                return promise;
            }, badName + " with " + size);

        });
    });

    // Call digest() with empty algorithm object
    Object.keys(sourceData).forEach(function(size) {
        promise_test(function(test) {
            var promise = subtle.digest({}, sourceData[size])
            .then(function(result) {
                assert_unreached("digest() with missing algorithm name should have thrown a TypeError");
            }, function(err) {
                assert_equals(err.name, "TypeError", "Missing algorithm name should cause TypeError")
            });

            return promise;
        }, "empty algorithm object with " + size);
    });


    done();


    // Returns a copy of the sourceBuffer it is sent.
    function copyBuffer(sourceBuffer) {
        var source = new Uint8Array(sourceBuffer);
        var copy = new Uint8Array(sourceBuffer.byteLength)

        for (var i=0; i<source.byteLength; i++) {
            copy[i] = source[i];
        }

        return copy;
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
