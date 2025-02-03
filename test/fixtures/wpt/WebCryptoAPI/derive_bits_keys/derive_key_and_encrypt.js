let iv = new Uint8Array(Array(12).keys());
let salt = new Uint8Array(Array(10).keys());
let plaintext = new Uint8Array(Array(100).keys());

function define_tests() {
    importKeys().then((keys) => {
        // Make sure that ecdh produces the same shared secret and the same encryption results using a key derived from that secret.
        keys.forEach(keyData => {
            promise_test(async() => {
                let hkdfKey = await crypto.subtle.deriveKey({name: "ECDH", public: keyData.publicKey }, keyData.privateKey, { name: "HKDF", hash: "" , salt: new Uint8Array(), info: new Uint8Array() }, false, ["deriveKey"]);
                let aesKey = await crypto.subtle.deriveKey({name: "HKDF", hash: "SHA-256", salt: salt, info: plaintext}, hkdfKey, {name:"AES-GCM", length: 256}, true, ["encrypt", "decrypt"]);
                let result = await crypto.subtle.encrypt({ name: "AES-GCM", iv: iv }, aesKey, plaintext);
                assert_equals(bytesToHexString(result), "a6280c522670eaf82f6564afbeb20a5b3f2d4e13c5596f6df3dcff8c34cb2118d2770fb24d83cfac5079c323118485bb01170292ee41eb82b07208f4840478fea3771d8922785c476ba06c2a0b933fc1661431419530a916ad4468545d1af5004a1149fea241c2ff1582ee58a8b7d79935de5def");
            }, "HKDF derivation of a ECDH key " + keyData.test);
            promise_test(async() => {
                let pkdf2Key = await crypto.subtle.deriveKey({name: "ECDH", public: keyData.publicKey }, keyData.privateKey, { name: "PBKDF2", hash: "" , salt: new Uint8Array(), iterations: 32 }, false, ["deriveKey"]);
                let aesKey = await crypto.subtle.deriveKey({name: "PBKDF2", hash: "SHA-256", salt: salt, iterations: 32 }, pkdf2Key, { name:"AES-GCM", length: 256 }, true, ["encrypt", "decrypt"]);
                let result = await crypto.subtle.encrypt({ name: "AES-GCM", iv: iv }, aesKey, plaintext);
                assert_equals(bytesToHexString(result), "c6201dfbb6fa92c1c246f6ce52f8f1c037f087efde41bac7f6485a2a8207623d2d3825b9cbe8ef864a90378667ed25544ce44cd2904bd96c19f0eeb611d626185165a8afb4e52f95700d7880f83939a42712fc4e377f198c01a61b397b76c3a4b93d932c321084bbef33332169dea09458b27df3");
            }, "PBKDF2 derivation of a ECDH key " + keyData.test);
        });
    }, (e) => {
        assert_unreached("Setup failed: " + e.message);
    });

    return Promise.resolve("define_tests");
}

async function importKeys() {
    // "ECDSA" with a 'P-256' curve
    let keyData = [
        hexStringToUint8Array("308187020100301306072a8648ce3d020106082a8648ce3d030107046d306b0201010420fe77a808a7109ba5ceb93ebebad2c84a714d864ad29b62d6537e1969035c0079a144034200042684c752eef1c927a80c74e8b02ce459f848b5977f37fd878b36dae632be9a6cadd56126e404a4f75c535e5769d95b49fb1106f784f3d231b776d1f4d57927ce"),
        hexStringToUint8Array("042684c752eef1c927a80c74e8b02ce459f848b5977f37fd878b36dae632be9a6cadd56126e404a4f75c535e5769d95b49fb1106f784f3d231b776d1f4d57927ce"),
        hexStringToUint8Array("308187020100301306072a8648ce3d020106082a8648ce3d030107046d306b020101042067521ccd1f85516118182bca3394c273bab9ce5cd6265105559e325e01f2df1ca144034200043042d8698882f2b59de972390d3fc9277e2e677a6c560148017c9475218fda1b38f76f7645fbcaf3d03e6259d080204fbafb04731b6ad53cb25c3d35d95b7c73"),
        hexStringToUint8Array("043042d8698882f2b59de972390d3fc9277e2e677a6c560148017c9475218fda1b38f76f7645fbcaf3d03e6259d080204fbafb04731b6ad53cb25c3d35d95b7c73"),
    ];
    let extractable = true;
    var allKeys = await Promise.all([
        crypto.subtle.importKey("pkcs8", keyData[0], {name: "ECDH", namedCurve: "P-256"}, extractable, ["deriveKey", 'deriveBits']),
        crypto.subtle.importKey("raw", keyData[1], {name: "ECDH", namedCurve: "P-256"}, extractable, []),
        crypto.subtle.importKey("pkcs8", keyData[2], {name: "ECDH", namedCurve: "P-256"}, extractable, ["deriveKey", 'deriveBits']),
        crypto.subtle.importKey("raw", keyData[3], {name: "ECDH", namedCurve: "P-256"}, extractable, []),
    ]);
    // Test cases defined combining public and private keys of each key-pair.
    return [
        { test: 1, publicKey: allKeys[3], privateKey: allKeys[0] },
        { test: 2, publicKey: allKeys[1], privateKey: allKeys[2] }
    ];
}
