
function run_test(algorithmName) {
  var subtle = self.crypto.subtle; // Change to test prefixed implementations

  // Source file [algorithm_name]_vectors.js provides the getTestVectors method
  // for the algorithm that drives these tests.
  var testVectors = getTestVectors(algorithmName);

  testVectors.forEach(function(vector) {
      var algorithm = {name: vector.algorithmName};

      // Test verification first, because signing tests rely on that working
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              isVerified = await subtle.verify(algorithm, key, vector.signature, vector.data)
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_true(isVerified, "Signature verified");
      }, vector.name + " verification");

      // Test verification with an altered buffer after call
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              var signature = copyBuffer(vector.signature);
              [isVerified] = await Promise.all([
                  subtle.verify(algorithm, key, signature, vector.data),
                  signature[0] = 255 - signature[0]
              ]);
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_true(isVerified, "Signature verified");
      }, vector.name + " verification with altered signature after call");

      // Check for successful verification even if data is altered after call.
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              var data = copyBuffer(vector.data);
              [isVerified] = await Promise.all([
                  subtle.verify(algorithm, key, vector.signature, data),
                  data[0] = 255 - data[0]
              ]);
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_true(isVerified, "Signature verified");
      }, vector.name + " with altered data after call");

      // Check for failures due to using privateKey to verify.
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("pkcs8", vector.privateKeyBuffer, algorithm, false, ["sign"]);
              isVerified = await subtle.verify(algorithm, key, vector.signature, vector.data)
              assert_unreached("Should have thrown error for using privateKey to verify in " + vector.name);
          } catch (err) {
              if (err instanceof AssertionError)
                  throw err;
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_equals(err.name, "InvalidAccessError", "Should throw InvalidAccessError instead of '" + err.message + "'");
          };
          assert_false(isVerified, "Signature verified");
      }, vector.name + " using privateKey to verify");

      // Check for failures due to using publicKey to sign.
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              let signature = await subtle.sign(algorithm, key, vector.data);
              assert_unreached("Should have thrown error for using publicKey to sign in " + vector.name);
          } catch (err) {
              if (err instanceof AssertionError)
                  throw err;
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_equals(err.name, "InvalidAccessError", "Should throw InvalidAccessError instead of '" + err.message + "'");
          };
      }, vector.name + " using publicKey to sign");

      // Check for failures due to no "verify" usage.
      promise_test(async() => {
          let isVerified = false;
          let key;
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, []);
              isVerified = await subtle.verify(algorithm, key, vector.signature, vector.data)
              assert_unreached("Should have thrown error for no verify usage in " + vector.name);
          } catch (err) {
              if (err instanceof AssertionError)
                  throw err;
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_equals(err.name, "InvalidAccessError", "Should throw InvalidAccessError instead of '" + err.message + "'");
          };
          assert_false(isVerified, "Signature verified");
      }, vector.name + " no verify usage");

      // Check for successful signing and verification.
      var algorithm = {name: vector.algorithmName};
      promise_test(async() => {
          let isVerified = false;
          let privateKey, publicKey;
          let signature;
          try {
              privateKey = await subtle.importKey("pkcs8", vector.privateKeyBuffer, algorithm, false, ["sign"]);
              publicKey = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              signature = await subtle.sign(algorithm, privateKey, vector.data);
              isVerified = await subtle.verify(algorithm, publicKey, vector.signature, vector.data)
          } catch (err) {
              assert_false(publicKey === undefined || privateKey === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_false(signature === undefined, "sign error for test " + vector.name + ": '" + err.message + "'");
              assert_unreached("verify error for test " + vector.name + ": " + err.message + "'");
          };
          assert_true(isVerified, "Round trip verification works");
      }, vector.name + " round trip");

      // Test signing with the wrong algorithm
      var algorithm = {name: vector.algorithmName};
      promise_test(async() => {
          let wrongKey;
          try {
              wrongKey = await subtle.generateKey({name: "HMAC", hash: "SHA-1"}, false, ["sign", "verify"])
              let signature = await subtle.sign(algorithm, wrongKey, vector.data);
              assert_unreached("Signing should not have succeeded for " + vector.name);
          } catch (err) {
              if (err instanceof AssertionError)
                  throw err;
              assert_false(wrongKey === undefined, "Generate wrong key for test " + vector.name + " failed: '" + err.message + "'");
              assert_equals(err.name, "InvalidAccessError", "Should have thrown InvalidAccessError instead of '" + err.message + "'");
          };
      }, vector.name + " signing with wrong algorithm name");

      // Test verification with the wrong algorithm
      var algorithm = {name: vector.algorithmName};
      promise_test(async() => {
          let wrongKey;
          try {
              wrongKey = await subtle.generateKey({name: "HMAC", hash: "SHA-1"}, false, ["sign", "verify"])
              let isVerified = await subtle.verify(algorithm, wrongKey, vector.signature, vector.data)
              assert_unreached("Verifying should not have succeeded for " + vector.name);
          } catch (err) {
              if (err instanceof AssertionError)
                  throw err;
              assert_false(wrongKey === undefined, "Generate wrong key for test " + vector.name + " failed: '" + err.message + "'");
              assert_equals(err.name, "InvalidAccessError", "Should have thrown InvalidAccessError instead of '" + err.message + "'");
          };
      }, vector.name + " verifying with wrong algorithm name");

      // Test verification fails with wrong signature
      var algorithm = {name: vector.algorithmName};
      promise_test(async() => {
          let key;
          let isVerified = true;
          var signature = copyBuffer(vector.signature);
          signature[0] = 255 - signature[0];
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              isVerified = await subtle.verify(algorithm, key, signature, vector.data)
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_false(isVerified, "Signature verified");
      }, vector.name + " verification failure due to altered signature");

      // Test verification fails with short (odd length) signature
      promise_test(async() => {
          let key;
          let isVerified = true;
          var signature = vector.signature.slice(1); // Skip the first byte
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              isVerified = await subtle.verify(algorithm, key, signature, vector.data)
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_false(isVerified, "Signature verified");
      }, vector.name + " verification failure due to shortened signature");

      // Test verification fails with wrong data
      promise_test(async() => {
          let key;
          let isVerified = true;
          var data = copyBuffer(vector.data);
          data[0] = 255 - data[0];
          try {
              key = await subtle.importKey("spki", vector.publicKeyBuffer, algorithm, false, ["verify"]);
              isVerified = await subtle.verify(algorithm, key, vector.signature, data)
          } catch (err) {
              assert_false(key === undefined, "importKey failed for " + vector.name + ". Message: ''" + err.message + "''");
              assert_unreached("Verification should not throw error " + vector.name + ": " + err.message + "'");
          };
          assert_false(isVerified, "Signature verified");
      }, vector.name + " verification failure due to altered data");

      // Test that generated keys are valid for signing and verifying.
      promise_test(async() => {
          let key = await subtle.generateKey(algorithm, false, ["sign", "verify"]);
          let signature = await subtle.sign(algorithm, key.privateKey, vector.data);
          let isVerified = await subtle.verify(algorithm, key.publicKey, signature, vector.data);
          assert_true(isVerified, "Verificaton failed.");
      }, "Sign and verify using generated " + vector.algorithmName + " keys.");
  });

  // Returns a copy of the sourceBuffer it is sent.
  function copyBuffer(sourceBuffer) {
      var source = new Uint8Array(sourceBuffer);
      var copy = new Uint8Array(sourceBuffer.byteLength)

      for (var i=0; i<source.byteLength; i++) {
          copy[i] = source[i];
      }

      return copy;
  }

  return;
}
