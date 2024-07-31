
function define_tests() {
  // May want to test prefixed implementations.
  var subtle = self.crypto.subtle;

  // Verify the derive functions perform checks against the all-zero value results,
  // ensuring small-order points are rejected.
  // https://www.rfc-editor.org/rfc/rfc7748#section-6.1
  // TODO: The spec states that the check must be done on use, but there is discussion about doing it on import.
  // https://github.com/WICG/webcrypto-secure-curves/pull/13
  Object.keys(kSmallOrderPoint).forEach(function(algorithmName) {
      kSmallOrderPoint[algorithmName].forEach(function(test) {
          promise_test(async() => {
              let derived;
              let privateKey;
              let publicKey;
              try {
                  privateKey = await subtle.importKey("pkcs8", pkcs8[algorithmName],
                                                  {name: algorithmName},
                                                  false, ["deriveBits", "deriveKey"]);
                  publicKey = await subtle.importKey("spki", test.vector,
                                                 {name: algorithmName},
                                                 false, [])
                  derived = await subtle.deriveBits({name: algorithmName, public: publicKey}, privateKey, 8 * sizes[algorithmName]);
              } catch (err) {
                  assert_false(privateKey === undefined, "Private key should be valid.");
                  assert_false(publicKey === undefined, "Public key should be valid.");
                  assert_equals(err.name, "OperationError", "Should throw correct error, not " + err.name + ": " + err.message + ".");
              }
              assert_equals(derived, undefined, "Operation succeeded, but should not have.");
          }, algorithmName + " key derivation checks for all-zero value result with a key of order " + test.order);
      });
  });

  return importKeys(pkcs8, spki, sizes)
  .then(function(results) {
      publicKeys = results.publicKeys;
      privateKeys = results.privateKeys;
      noDeriveBitsKeys = results.noDeriveBitsKeys;

      Object.keys(sizes).forEach(function(algorithmName) {
          // Basic success case
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_true(equalBuffers(derivation, derivations[algorithmName]), "Derived correct bits");
              }, function(err) {
                  assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
              });
          }, algorithmName + " good parameters");

          // Case insensitivity check
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName.toLowerCase(), public: publicKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_true(equalBuffers(derivation, derivations[algorithmName]), "Derived correct bits");
              }, function(err) {
                  assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
              });
          }, algorithmName + " mixed case parameters");

          // Null length
          // "Null" is not valid per the current spec
          //   - https://github.com/w3c/webcrypto/issues/322
          //   - https://github.com/w3c/webcrypto/issues/329
          //
          // Proposal for a spec change:
          //   - https://github.com/w3c/webcrypto/pull/345
          //
          // This test case may be replaced by these new tests:
          //   - https://github.com/web-platform-tests/wpt/pull/43400
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, privateKeys[algorithmName], null)
              .then(function(derivation) {
                  assert_true(equalBuffers(derivation, derivations[algorithmName]), "Derived correct bits");
              }, function(err) {
                  assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
              });
          }, algorithmName + " with null length");

          // Shorter than entire derivation per algorithm
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName] - 32)
              .then(function(derivation) {
                  assert_true(equalBuffers(derivation, derivations[algorithmName], 8 * sizes[algorithmName] - 32), "Derived correct bits");
              }, function(err) {
                  assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
              });
          }, algorithmName + " short result");

          // Non-multiple of 8
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName] - 11)
              .then(function(derivation) {
                  assert_true(equalBuffers(derivation, derivations[algorithmName], 8 * sizes[algorithmName] - 11), "Derived correct bits");
              }, function(err) {
                  assert_unreached("deriveBits failed with error " + err.name + ": " + err.message);
              });
          }, algorithmName + " non-multiple of 8 bits");

          // Errors to test:

          // - missing public property TypeError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with TypeError");
              }, function(err) {
                  assert_equals(err.name, "TypeError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " missing public property");

          // - Non CryptoKey public property TypeError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: {message: "Not a CryptoKey"}}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with TypeError");
              }, function(err) {
                  assert_equals(err.name, "TypeError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " public property of algorithm is not a CryptoKey");

          // - wrong algorithm
          promise_test(function(test) {
              publicKey = publicKeys["X25519"];
              if (algorithmName === "X25519") {
                  publicKey = publicKeys["X448"];
              }
              return subtle.deriveBits({name: algorithmName, public: publicKey}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
              }, function(err) {
                  assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " mismatched algorithms");

          // - No deriveBits usage in baseKey InvalidAccessError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, noDeriveBitsKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
              }, function(err) {
                  assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " no deriveBits usage for base key");

          // - Use public key for baseKey InvalidAccessError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, publicKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
              }, function(err) {
                  assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " base key is not a private key");

          // - Use private key for public property InvalidAccessError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: privateKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName])
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
              }, function(err) {
                  assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " public property value is a private key");

          // - Use secret key for public property InvalidAccessError
          promise_test(function(test) {
              return subtle.generateKey({name: "AES-CBC", length: 128}, true, ["encrypt", "decrypt"])
              .then(function(secretKey) {
                  return subtle.deriveBits({name: algorithmName, public: secretKey}, privateKeys[algorithmName], 8 * sizes[algorithmName])
                  .then(function(derivation) {
                      assert_unreached("deriveBits succeeded but should have failed with InvalidAccessError");
                  }, function(err) {
                      assert_equals(err.name, "InvalidAccessError", "Should throw correct error, not " + err.name + ": " + err.message);
                  });
              });
          }, algorithmName + " public property value is a secret key");

          // - Length greater than possible for particular curves OperationError
          promise_test(function(test) {
              return subtle.deriveBits({name: algorithmName, public: publicKeys[algorithmName]}, privateKeys[algorithmName], 8 * sizes[algorithmName] + 8)
              .then(function(derivation) {
                  assert_unreached("deriveBits succeeded but should have failed with OperationError");
              }, function(err) {
                  assert_equals(err.name, "OperationError", "Should throw correct error, not " + err.name + ": " + err.message);
              });
          }, algorithmName + " asking for too many bits");
      });
  });

  function importKeys(pkcs8, spki, sizes) {
      var privateKeys = {};
      var publicKeys = {};
      var noDeriveBitsKeys = {};

      var promises = [];
      Object.keys(pkcs8).forEach(function(algorithmName) {
          var operation = subtle.importKey("pkcs8", pkcs8[algorithmName],
                                          {name: algorithmName},
                                          false, ["deriveBits", "deriveKey"])
                          .then(function(key) {
                              privateKeys[algorithmName] = key;
                            }, function (err) {
                              privateKeys[algorithmName] = null;
                          });
          promises.push(operation);
      });
      Object.keys(pkcs8).forEach(function(algorithmName) {
          var operation = subtle.importKey("pkcs8", pkcs8[algorithmName],
                                          {name: algorithmName},
                                          false, ["deriveKey"])
                          .then(function(key) {
                              noDeriveBitsKeys[algorithmName] = key;
                            }, function (err) {
                              noDeriveBitsKeys[algorithmName] = null;
                          });
          promises.push(operation);
      });
      Object.keys(spki).forEach(function(algorithmName) {
          var operation = subtle.importKey("spki", spki[algorithmName],
                                          {name: algorithmName},
                                          false, [])
                          .then(function(key) {
                              publicKeys[algorithmName] = key;
                            }, function (err) {
                              publicKeys[algorithmName] = null;
                          });
          promises.push(operation);
      });

      return Promise.all(promises)
             .then(function(results) {return {privateKeys: privateKeys, publicKeys: publicKeys, noDeriveBitsKeys: noDeriveBitsKeys}});
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
