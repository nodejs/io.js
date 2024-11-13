//
// helpers.js
//
// Helper functions used by several WebCryptoAPI tests
//

var registeredAlgorithmNames = [
    "RSASSA-PKCS1-v1_5",
    "RSA-PSS",
    "RSA-OAEP",
    "ECDSA",
    "ECDH",
    "AES-CTR",
    "AES-CBC",
    "AES-GCM",
    "AES-KW",
    "HMAC",
    "SHA-1",
    "SHA-256",
    "SHA-384",
    "SHA-512",
    "HKDF",
    "PBKDF2",
    "Ed25519",
    "Ed448",
    "X25519",
    "X448"
];


// Treats an array as a set, and generates an array of all non-empty
// subsets (which are themselves arrays).
//
// The order of members of the "subsets" is not guaranteed.
function allNonemptySubsetsOf(arr) {
    var results = [];
    var firstElement;
    var remainingElements;

    for(var i=0; i<arr.length; i++) {
        firstElement = arr[i];
        remainingElements = arr.slice(i+1);
        results.push([firstElement]);

        if (remainingElements.length > 0) {
            allNonemptySubsetsOf(remainingElements).forEach(function(combination) {
                combination.push(firstElement);
                results.push(combination);
            });
        }
    }

    return results;
}


// Create a string representation of keyGeneration parameters for
// test names and labels.
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

// Is key a CryptoKey object with correct algorithm, extractable, and usages?
// Is it a secret, private, or public kind of key?
function assert_goodCryptoKey(key, algorithm, extractable, usages, kind) {
    var correctUsages = [];

    var registeredAlgorithmName;
    registeredAlgorithmNames.forEach(function(name) {
        if (name.toUpperCase() === algorithm.name.toUpperCase()) {
            registeredAlgorithmName = name;
        }
    });

    assert_equals(key.constructor, CryptoKey, "Is a CryptoKey");
    assert_equals(key.type, kind, "Is a " + kind + " key");
    assert_equals(key.extractable, extractable, "Extractability is correct");

    assert_equals(key.algorithm.name, registeredAlgorithmName, "Correct algorithm name");
    if (key.algorithm.name.toUpperCase() === "HMAC" && algorithm.length === undefined) {
        switch (key.algorithm.hash.name.toUpperCase()) {
            case 'SHA-1':
            case 'SHA-256':
                assert_equals(key.algorithm.length, 512, "Correct length");
                break;
            case 'SHA-384':
            case 'SHA-512':
                assert_equals(key.algorithm.length, 1024, "Correct length");
                break;
            default:
                assert_unreached("Unrecognized hash");
        }
    } else {
        assert_equals(key.algorithm.length, algorithm.length, "Correct length");
    }
    if (["HMAC", "RSASSA-PKCS1-v1_5", "RSA-PSS"].includes(registeredAlgorithmName)) {
        assert_equals(key.algorithm.hash.name.toUpperCase(), algorithm.hash.toUpperCase(), "Correct hash function");
    }

    if (/^(?:Ed|X)(?:25519|448)$/.test(key.algorithm.name)) {
        assert_false('namedCurve' in key.algorithm, "Does not have a namedCurve property");
    }

    // usages is expected to be provided for a key pair, but we are checking
    // only a single key. The publicKey and privateKey portions of a key pair
    // recognize only some of the usages appropriate for a key pair.
    if (key.type === "public") {
        ["encrypt", "verify", "wrapKey"].forEach(function(usage) {
            if (usages.includes(usage)) {
                correctUsages.push(usage);
            }
        });
    } else if (key.type === "private") {
        ["decrypt", "sign", "unwrapKey", "deriveKey", "deriveBits"].forEach(function(usage) {
            if (usages.includes(usage)) {
                correctUsages.push(usage);
            }
        });
    } else {
        correctUsages = usages;
    }

    assert_equals((typeof key.usages), "object", key.type + " key.usages is an object");
    assert_not_equals(key.usages, null, key.type + " key.usages isn't null");

    // The usages parameter could have repeats, but the usages
    // property of the result should not.
    var usageCount = 0;
    key.usages.forEach(function(usage) {
        usageCount += 1;
        assert_in_array(usage, correctUsages, "Has " + usage + " usage");
    });
    assert_equals(key.usages.length, usageCount, "usages property is correct");
    assert_equals(key[Symbol.toStringTag], 'CryptoKey', "has the expected Symbol.toStringTag");
}


// The algorithm parameter is an object with a name and other
// properties. Given the name, generate all valid parameters.
function allAlgorithmSpecifiersFor(algorithmName) {
    var results = [];

    // RSA key generation is slow. Test a minimal set of parameters
    var hashes = ["SHA-1", "SHA-256"];

    // EC key generation is a lot faster. Check all curves in the spec
    var curves = ["P-256", "P-384", "P-521"];

    if (algorithmName.toUpperCase().substring(0, 3) === "AES") {
        // Specifier properties are name and length
        [128, 192, 256].forEach(function(length) {
            results.push({name: algorithmName, length: length});
        });
    } else if (algorithmName.toUpperCase() === "HMAC") {
        [
            {hash: "SHA-1", length: 160},
            {hash: "SHA-256", length: 256},
            {hash: "SHA-384", length: 384},
            {hash: "SHA-512", length: 512},
            {hash: "SHA-1"},
            {hash: "SHA-256"},
            {hash: "SHA-384"},
            {hash: "SHA-512"},
        ].forEach(function(hashAlgorithm) {
            results.push({name: algorithmName, ...hashAlgorithm});
        });
    } else if (algorithmName.toUpperCase().substring(0, 3) === "RSA") {
        hashes.forEach(function(hashName) {
            results.push({name: algorithmName, hash: hashName, modulusLength: 2048, publicExponent: new Uint8Array([1,0,1])});
        });
    } else if (algorithmName.toUpperCase().substring(0, 2) === "EC") {
        curves.forEach(function(curveName) {
            results.push({name: algorithmName, namedCurve: curveName});
        });
    } else if (algorithmName.toUpperCase().substring(0, 1) === "X" || algorithmName.toUpperCase().substring(0, 2) === "ED") {
        results.push({ name: algorithmName });
    }

    return results;
}


// Create every possible valid usages parameter, given legal
// usages. Note that an empty usages parameter is not always valid.
//
// There is an optional parameter - mandatoryUsages. If provided,
// it should be an array containing those usages of which one must be
// included.
function allValidUsages(validUsages, emptyIsValid, mandatoryUsages) {
    if (typeof mandatoryUsages === "undefined") {
        mandatoryUsages = [];
    }

    var okaySubsets = [];
    allNonemptySubsetsOf(validUsages).forEach(function(subset) {
        if (mandatoryUsages.length === 0) {
            okaySubsets.push(subset);
        } else {
            for (var i=0; i<mandatoryUsages.length; i++) {
                if (subset.includes(mandatoryUsages[i])) {
                    okaySubsets.push(subset);
                    return;
                }
            }
        }
    });

    if (emptyIsValid && validUsages.length !== 0) {
        okaySubsets.push([]);
    }

    okaySubsets.push(validUsages.concat(mandatoryUsages).concat(validUsages)); // Repeated values are allowed
    return okaySubsets;
}

function unique(names) {
    return [...new Set(names)];
}

// Algorithm name specifiers are case-insensitive. Generate several
// case variations of a given name.
function allNameVariants(name, slowTest) {
    var upCaseName = name.toUpperCase();
    var lowCaseName = name.toLowerCase();
    var mixedCaseName = upCaseName.substring(0, 1) + lowCaseName.substring(1);

    // for slow tests effectively cut the amount of work in third by only
    // returning one variation
    if (slowTest) return [mixedCaseName];
    return unique([upCaseName, lowCaseName, mixedCaseName]);
}

// Builds a hex string representation for an array-like input.
// "bytes" can be an Array of bytes, an ArrayBuffer, or any TypedArray.
// The output looks like this:
//    ab034c99
function bytesToHexString(bytes)
{
    if (!bytes)
        return null;

    bytes = new Uint8Array(bytes);
    var hexBytes = [];

    for (var i = 0; i < bytes.length; ++i) {
        var byteString = bytes[i].toString(16);
        if (byteString.length < 2)
            byteString = "0" + byteString;
        hexBytes.push(byteString);
    }

    return hexBytes.join("");
}

function hexStringToUint8Array(hexString)
{
    if (hexString.length % 2 != 0)
        throw "Invalid hexString";
    var arrayBuffer = new Uint8Array(hexString.length / 2);

    for (var i = 0; i < hexString.length; i += 2) {
        var byteValue = parseInt(hexString.substr(i, 2), 16);
        if (byteValue == NaN)
            throw "Invalid hexString";
        arrayBuffer[i/2] = byteValue;
    }

    return arrayBuffer;
}
