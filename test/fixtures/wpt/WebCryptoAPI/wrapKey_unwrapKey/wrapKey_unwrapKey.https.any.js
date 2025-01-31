// META: title=WebCryptoAPI: wrapKey() and unwrapKey()
// META: timeout=long
// META: script=../util/helpers.js
// META: script=wrapKey_unwrapKey_vectors.js

// Tests for wrapKey and unwrapKey round tripping

    var subtle = self.crypto.subtle;

    var wrappers = {};  // Things we wrap (and upwrap) keys with
    var keys = {};      // Things to wrap and unwrap

    // There are five algorithms that can be used for wrapKey/unwrapKey.
    // Generate one key with typical parameters for each kind.
    //
    // Note: we don't need cryptographically strong parameters for things
    // like IV - just any legal value will do.
    var wrappingKeysParameters = [
        {
            name: "RSA-OAEP",
            importParameters: {name: "RSA-OAEP", hash: "SHA-256"},
            wrapParameters: {name: "RSA-OAEP", label: new Uint8Array(8)}
        },
        {
            name: "AES-CTR",
            importParameters: {name: "AES-CTR", length: 128},
            wrapParameters: {name: "AES-CTR", counter: new Uint8Array(16), length: 64}
        },
        {
            name: "AES-CBC",
            importParameters: {name: "AES-CBC", length: 128},
            wrapParameters: {name: "AES-CBC", iv: new Uint8Array(16)}
        },
        {
            name: "AES-GCM",
            importParameters: {name: "AES-GCM", length: 128},
            wrapParameters: {name: "AES-GCM", iv: new Uint8Array(16), additionalData: new Uint8Array(16), tagLength: 128}
        },
        {
            name: "AES-KW",
            importParameters: {name: "AES-KW", length: 128},
            wrapParameters: {name: "AES-KW"}
        }
    ];

    var keysToWrapParameters = [
        {algorithm: {name: "RSASSA-PKCS1-v1_5", hash: "SHA-256"}, privateUsages: ["sign"], publicUsages: ["verify"]},
        {algorithm: {name: "RSA-PSS", hash: "SHA-256"}, privateUsages: ["sign"], publicUsages: ["verify"]},
        {algorithm: {name: "RSA-OAEP", hash: "SHA-256"}, privateUsages: ["decrypt"], publicUsages: ["encrypt"]},
        {algorithm: {name: "ECDSA", namedCurve: "P-256"}, privateUsages: ["sign"], publicUsages: ["verify"]},
        {algorithm: {name: "ECDH", namedCurve: "P-256"}, privateUsages: ["deriveBits"], publicUsages: []},
        {algorithm: {name: "Ed25519" }, privateUsages: ["sign"], publicUsages: ["verify"]},
        {algorithm: {name: "Ed448" }, privateUsages: ["sign"], publicUsages: ["verify"]},
        {algorithm: {name: "X25519" }, privateUsages: ["deriveBits"], publicUsages: []},
        {algorithm: {name: "X448" }, privateUsages: ["deriveBits"], publicUsages: []},
        {algorithm: {name: "AES-CTR", length: 128}, usages: ["encrypt", "decrypt"]},
        {algorithm: {name: "AES-CBC", length: 128}, usages: ["encrypt", "decrypt"]},
        {algorithm: {name: "AES-GCM", length: 128}, usages: ["encrypt", "decrypt"]},
        {algorithm: {name: "AES-KW", length: 128}, usages: ["wrapKey", "unwrapKey"]},
        {algorithm: {name: "HMAC", length: 128, hash: "SHA-256"}, usages: ["sign", "verify"]}
    ];

    // Import all the keys needed, then iterate over all combinations
    // to test wrapping and unwrapping.
    promise_test(function() {
    return Promise.all([importWrappingKeys(), importKeysToWrap()])
    .then(function(results) {
        wrappingKeysParameters.filter((param) => Object.keys(wrappers).includes(param.name)).forEach(function(wrapperParam) {
            var wrapper = wrappers[wrapperParam.name];
            keysToWrapParameters.filter((param) => Object.keys(keys).includes(param.algorithm.name)).forEach(function(toWrapParam) {
                var keyData = keys[toWrapParam.algorithm.name];
                ["raw", "spki", "pkcs8"].filter((fmt) => Object.keys(keyData).includes(fmt)).forEach(function(keyDataFormat) {
                    var toWrap = keyData[keyDataFormat];
                    [keyDataFormat, "jwk"].forEach(function(format) {
                        if (wrappingIsPossible(toWrap.originalExport[format], wrapper.parameters.name)) {
                            testWrapping(wrapper, toWrap, format);
                            if (canCompareNonExtractableKeys(toWrap.key)) {
                                testWrappingNonExtractable(wrapper, toWrap, format);
                                if (format === "jwk") {
                                    testWrappingNonExtractableAsExtractable(wrapper, toWrap);
                                }
                            }
                        }
                    });
                });
            });
        });
        return Promise.resolve("setup done");
    }, function(err) {
        return Promise.reject("setup failed: " + err.name + ': "' + err.message + '"');
    });
    }, "setup");

    function importWrappingKeys() {
        // Using allSettled to skip unsupported test cases.
        var promises = [];
        wrappingKeysParameters.forEach(function(params) {
            if (params.name === "RSA-OAEP") { // we have a key pair, not just a key
                var algorithm = {name: "RSA-OAEP", hash: "SHA-256"};
                wrappers[params.name] = {wrappingKey: undefined, unwrappingKey: undefined, parameters: params};
                promises.push(subtle.importKey("spki", wrappingKeyData["RSA"].spki, algorithm, true, ["wrapKey"])
                              .then(function(key) {
                                  wrappers["RSA-OAEP"].wrappingKey = key;
                              }));
                promises.push(subtle.importKey("pkcs8", wrappingKeyData["RSA"].pkcs8, algorithm, true, ["unwrapKey"])
                              .then(function(key) {
                                  wrappers["RSA-OAEP"].unwrappingKey = key;
                              }));
            } else {
                var algorithm = {name: params.name};
                promises.push(subtle.importKey("raw", wrappingKeyData["SYMMETRIC"].raw, algorithm, true, ["wrapKey", "unwrapKey"])
                              .then(function(key) {
                                  wrappers[params.name] = {wrappingKey: key, unwrappingKey: key, parameters: params};
                              }));
            }
        });
        // Using allSettled to skip unsupported test cases.
        return Promise.allSettled(promises);
    }

    async function importAndExport(format, keyData, algorithm, keyUsages, keyType) {
        var importedKey;
        try {
            importedKey = await subtle.importKey(format, keyData, algorithm, true, keyUsages);
            keys[algorithm.name][format] = { name: algorithm.name + " " + keyType, algorithm: algorithm, usages: keyUsages, key: importedKey, originalExport: {} };
        } catch (err) {
            delete keys[algorithm.name][format];
            throw("Error importing " + algorithm.name + " " + keyType + " key in '" + format + "' -" + err.name + ': "' + err.message + '"');
        };
        try {
            var exportedKey = await subtle.exportKey(format, importedKey);
            keys[algorithm.name][format].originalExport[format] = exportedKey;
        } catch (err) {
            delete keys[algorithm.name][format];
            throw("Error exporting " + algorithm.name + " '" + format + "' key -" + err.name + ': "' + err.message + '"');
        };
        try {
            var jwkExportedKey = await subtle.exportKey("jwk", importedKey);
            keys[algorithm.name][format].originalExport["jwk"] = jwkExportedKey;
        } catch (err) {
            delete keys[algorithm.name][format];
            throw("Error exporting " + algorithm.name + " '" + format + "' key to 'jwk' -" + err.name + ': "' + err.message + '"');
        };
    }

    function importKeysToWrap() {
        var promises = [];
        keysToWrapParameters.forEach(function(params) {
            if ("publicUsages" in params) {
                keys[params.algorithm.name] = {};
                var keyData = toWrapKeyDataFromAlg(params.algorithm.name);
                promises.push(importAndExport("spki", keyData.spki, params.algorithm, params.publicUsages, "public key "));
                promises.push(importAndExport("pkcs8", keyData.pkcs8, params.algorithm, params.privateUsages, "private key "));
            } else {
                keys[params.algorithm.name] = {};
                promises.push(importAndExport("raw", toWrapKeyData["SYMMETRIC"].raw, params.algorithm, params.usages, ""));
            }
        });
        // Using allSettled to skip unsupported test cases.
        return Promise.allSettled(promises);
    }

    // Can we successfully "round-trip" (wrap, then unwrap, a key)?
    function testWrapping(wrapper, toWrap, fmt) {
        promise_test(async() => {
            try {
                var wrappedResult = await subtle.wrapKey(fmt, toWrap.key, wrapper.wrappingKey, wrapper.parameters.wrapParameters);
                var unwrappedResult = await subtle.unwrapKey(fmt, wrappedResult, wrapper.unwrappingKey, wrapper.parameters.wrapParameters, toWrap.algorithm, true, toWrap.usages);
                assert_goodCryptoKey(unwrappedResult, toWrap.algorithm, true, toWrap.usages, toWrap.key.type);
                var roundTripExport = await subtle.exportKey(fmt, unwrappedResult);
                assert_true(equalExport(toWrap.originalExport[fmt], roundTripExport), "Post-wrap export matches original export");
            } catch (err) {
                if (err instanceof AssertionError) {
                    throw err;
                }
                assert_unreached("Round trip for extractable key threw an error - " + err.name + ': "' + err.message + '"');
            }
        }, "Can wrap and unwrap " + toWrap.name + "keys using " + fmt + " and " + wrapper.parameters.name);
    }

    function testWrappingNonExtractable(wrapper, toWrap, fmt) {
        promise_test(async() => {
            try {
                var wrappedResult = await subtle.wrapKey(fmt, toWrap.key, wrapper.wrappingKey, wrapper.parameters.wrapParameters);
                var unwrappedResult = await subtle.unwrapKey(fmt, wrappedResult, wrapper.unwrappingKey, wrapper.parameters.wrapParameters, toWrap.algorithm, false, toWrap.usages);
                assert_goodCryptoKey(unwrappedResult, toWrap.algorithm, false, toWrap.usages, toWrap.key.type);
                var result = await equalKeys(toWrap.key, unwrappedResult);
                assert_true(result, "Unwrapped key matches original");
            } catch (err) {
                if (err instanceof AssertionError) {
                    throw err;
                }
                assert_unreached("Round trip for key unwrapped non-extractable threw an error - " + err.name + ': "' + err.message + '"');
            };
        }, "Can wrap and unwrap " + toWrap.name + "keys as non-extractable using " + fmt + " and " + wrapper.parameters.name);
    }

    function testWrappingNonExtractableAsExtractable(wrapper, toWrap) {
        promise_test(async() => {
            var wrappedKey;
            try {
                var wrappedResult = await wrapAsNonExtractableJwk(toWrap.key,wrapper);
                wrappedKey = wrappedResult;
                var unwrappedResult = await subtle.unwrapKey("jwk", wrappedKey, wrapper.unwrappingKey, wrapper.parameters.wrapParameters, toWrap.algorithm, false, toWrap.usages);
                assert_false(unwrappedResult.extractable, "Unwrapped key is non-extractable");
                var result = await equalKeys(toWrap.key,unwrappedResult);
                assert_true(result, "Unwrapped key matches original");
            } catch (err) {
                if (err instanceof AssertionError) {
                    throw err;
                }
                assert_unreached("Round trip for non-extractable key threw an error - " + err.name + ': "' + err.message + '"');
            };
            try {
                var unwrappedResult = await subtle.unwrapKey("jwk", wrappedKey, wrapper.unwrappingKey, wrapper.parameters.wrapParameters, toWrap.algorithm, true, toWrap.usages);
                assert_unreached("Unwrapping a non-extractable JWK as extractable should fail");
            } catch (err) {
                if (err instanceof AssertionError) {
                    throw err;
                }
                assert_equals(err.name, "DataError", "Unwrapping a non-extractable JWK as extractable fails with DataError");
            }
        }, "Can unwrap " + toWrap.name + "non-extractable keys using jwk and " + wrapper.parameters.name);
    }

    // Implement key wrapping by hand to wrap a key as non-extractable JWK
    async function wrapAsNonExtractableJwk(key, wrapper) {
        var wrappingKey = wrapper.wrappingKey,
            encryptKey;

        var jwkWrappingKey = await subtle.exportKey("jwk",wrappingKey);
        // Update the key generation parameters to work as key import parameters
        var params = Object.create(wrapper.parameters.importParameters);
        if(params.name === "AES-KW") {
            params.name = "AES-CBC";
            jwkWrappingKey.alg = "A"+params.length+"CBC";
        } else if (params.name === "RSA-OAEP") {
            params.modulusLength = undefined;
            params.publicExponent = undefined;
        }
        jwkWrappingKey.key_ops = ["encrypt"];
        var importedWrappingKey = await subtle.importKey("jwk", jwkWrappingKey, params, true, ["encrypt"]);
        encryptKey = importedWrappingKey;
        var exportedKey = await subtle.exportKey("jwk",key);
        exportedKey.ext = false;
        var jwk = JSON.stringify(exportedKey)
        var result;
        if (wrappingKey.algorithm.name === "AES-KW") {
            result = await aeskw(encryptKey, str2ab(jwk.slice(0,-1) + " ".repeat(jwk.length%8 ? 8-jwk.length%8 : 0) + "}"));
        } else {
            result = await subtle.encrypt(wrapper.parameters.wrapParameters,encryptKey,str2ab(jwk));
        }
        return result;
    }


    // RSA-OAEP can only wrap relatively small payloads. AES-KW can only
    // wrap payloads a multiple of 8 bytes long.
    function wrappingIsPossible(exportedKey, algorithmName) {
        if ("byteLength" in exportedKey && algorithmName === "AES-KW") {
            return exportedKey.byteLength % 8 === 0;
        }

        if ("byteLength" in exportedKey && algorithmName === "RSA-OAEP") {
            // RSA-OAEP can only encrypt payloads with lengths shorter
            // than modulusLength - 2*hashLength - 1 bytes long. For
            // a 4096 bit modulus and SHA-256, that comes to
            // 4096/8 - 2*(256/8) - 1 = 512 - 2*32 - 1 = 447 bytes.
            return exportedKey.byteLength <= 446;
        }

        if ("kty" in exportedKey && algorithmName === "AES-KW") {
            return JSON.stringify(exportedKey).length % 8 == 0;
        }

        if ("kty" in exportedKey && algorithmName === "RSA-OAEP") {
            return JSON.stringify(exportedKey).length <= 478;
        }

        return true;
    }


    // Helper methods follow:

    // Are two exported keys equal
    function equalExport(originalExport, roundTripExport) {
        if ("byteLength" in originalExport) {
            return equalBuffers(originalExport, roundTripExport);
        } else {
            return equalJwk(originalExport, roundTripExport);
        }
    }

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
            if (objectToString(expected[fieldName]) !== objectToString(got[fieldName])) {
                return false;
            }
        }

        return true;
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

    // Can we compare key values by using them
    function canCompareNonExtractableKeys(key){
        if (key.usages.indexOf("decrypt") !== -1) {
            return true;
        }
        if (key.usages.indexOf("sign") !== -1) {
            return true;
        }
        if (key.usages.indexOf("wrapKey") !== -1) {
            return true;
        }
        if (key.usages.indexOf("deriveBits") !== -1) {
            return true;
        }
        return false;
    }

    // Compare two keys by using them (works for non-extractable keys)
    async function equalKeys(expected, got){
        if ( expected.algorithm.name !== got.algorithm.name ) {
            return false;
        }

        var cryptParams, signParams, wrapParams, deriveParams;
        switch(expected.algorithm.name){
            case "AES-CTR" :
                cryptParams = {name: "AES-CTR", counter: new Uint8Array(16), length: 64};
                break;
            case "AES-CBC" :
                cryptParams = {name: "AES-CBC", iv: new Uint8Array(16) };
                break;
            case "AES-GCM" :
                cryptParams = {name: "AES-GCM", iv: new Uint8Array(16) };
                break;
            case "RSA-OAEP" :
                cryptParams = {name: "RSA-OAEP", label: new Uint8Array(8) };
                break;
            case "RSASSA-PKCS1-v1_5" :
                signParams = {name: "RSASSA-PKCS1-v1_5"};
                break;
            case "RSA-PSS" :
                signParams = {name: "RSA-PSS", saltLength: 32 };
                break;
            case "ECDSA" :
                signParams = {name: "ECDSA", hash: "SHA-256"};
                break;
            case "Ed25519" :
                signParams = {name: "Ed25519"};
                break;
            case "Ed448" :
                signParams = {name: "Ed448"};
                break;
            case "X25519" :
                deriveParams = {name: "X25519"};
                break;
            case "X448" :
                deriveParams = {name: "X448"};
                break;
            case "HMAC" :
                signParams = {name: "HMAC"};
                break;
            case "AES-KW" :
                wrapParams = {name: "AES-KW"};
                break;
            case "ECDH" :
                deriveParams = {name: "ECDH"};
                break;
            default:
                throw new Error("Unsupported algorithm for key comparison");
        }

        if (cryptParams) {
            var jwkExpectedKey = await subtle.exportKey("jwk", expected);
            if (expected.algorithm.name === "RSA-OAEP") {
                ["d","p","q","dp","dq","qi","oth"].forEach(function(field){ delete jwkExpectedKey[field]; });
            }
            jwkExpectedKey.key_ops = ["encrypt"];
            var expectedEncryptKey = await subtle.importKey("jwk", jwkExpectedKey, expected.algorithm, true, ["encrypt"]);
            var encryptedData = await subtle.encrypt(cryptParams, expectedEncryptKey, new Uint8Array(32));
            var decryptedData = await subtle.decrypt(cryptParams, got, encryptedData);
            var result = new Uint8Array(decryptedData);
            return !result.some(x => x);
        } else if (signParams) {
            var verifyKey;
            var jwkExpectedKey = await subtle.exportKey("jwk",expected);
            if (expected.algorithm.name === "RSA-PSS" || expected.algorithm.name === "RSASSA-PKCS1-v1_5") {
                ["d","p","q","dp","dq","qi","oth"].forEach(function(field){ delete jwkExpectedKey[field]; });
            }
            if (expected.algorithm.name === "ECDSA" || expected.algorithm.name.startsWith("Ed")) {
                delete jwkExpectedKey["d"];
            }
            jwkExpectedKey.key_ops = ["verify"];
            var expectedVerifyKey = await subtle.importKey("jwk", jwkExpectedKey, expected.algorithm, true, ["verify"]);
            verifyKey = expectedVerifyKey;
            var signature = await subtle.sign(signParams, got, new Uint8Array(32));
            var result = await subtle.verify(signParams, verifyKey, signature, new Uint8Array(32));
            return result;
        } else if (wrapParams) {
            var aKeyToWrap, wrappedWithExpected;
            var key = await subtle.importKey("raw",new Uint8Array(16), "AES-CBC", true, ["encrypt"])
            aKeyToWrap = key;
            var wrapResult = await subtle.wrapKey("raw", aKeyToWrap, expected, wrapParams);
            wrappedWithExpected = Array.from((new Uint8Array(wrapResult)).values());
            wrapResult = await subtle.wrapKey("raw", aKeyToWrap, got, wrapParams);
            var wrappedWithGot = Array.from((new Uint8Array(wrapResult)).values());
            return wrappedWithGot.every((x,i) => x === wrappedWithExpected[i]);
        } else if (deriveParams) {
            var expectedDerivedBits;
            var key = await subtle.generateKey(expected.algorithm, true, ['deriveBits']);
            deriveParams.public = key.publicKey;
            var result = await subtle.deriveBits(deriveParams, expected, 128);
            expectedDerivedBits = Array.from((new Uint8Array(result)).values());
            result = await subtle.deriveBits(deriveParams, got, 128);
            var gotDerivedBits = Array.from((new Uint8Array(result)).values());
            return gotDerivedBits.every((x,i) => x === expectedDerivedBits[i]);
        }
    }

    // Raw AES encryption
    async function aes(k, p) {
        const ciphertext = await subtle.encrypt({ name: "AES-CBC", iv: new Uint8Array(16) }, k, p);
        return ciphertext.slice(0, 16);
    }

    // AES Key Wrap
    async function aeskw(key, data) {
        if (data.byteLength % 8 !== 0) {
            throw new Error("AES Key Wrap data must be a multiple of 8 bytes in length");
        }

        var A = Uint8Array.from([0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0, 0, 0, 0, 0, 0, 0, 0]),
            Av = new DataView(A.buffer),
            R = [],
            n = data.byteLength / 8;

        for(var i = 0; i<data.byteLength; i+=8) {
            R.push(new Uint8Array(data.slice(i,i+8)));
        }

        async function aeskw_step(j, i, final, B) {
            A.set(new Uint8Array(B.slice(0,8)));
            Av.setUint32(4,Av.getUint32(4) ^ (n*j+i+1));
            R[i] = new Uint8Array(B.slice(8,16));
            if (final) {
                R.unshift(A.slice(0,8));
                var result = new Uint8Array(R.length * 8);
                R.forEach(function(Ri,i){ result.set(Ri, i*8); });
                return result;
            } else {
                A.set(R[(i+1)%n],8);
                return aes(key,A);
            }
        }

        A.set(R[0], 8);
        let B = await aes(key, A);

        for(var j=0;j<6;++j) {
            for(var i=0;i<n;++i) {
                B = await aeskw_step(j, i, j === 5 && i === (n - 1), B);
            }
        }

        return B;
    }

    function str2ab(str)        { return Uint8Array.from( str.split(''), function(s){return s.charCodeAt(0)} ); }
    function ab2str(ab)         { return String.fromCharCode.apply(null, new Uint8Array(ab)); }

