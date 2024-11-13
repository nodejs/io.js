
function run_test() {
  var subtle = self.crypto.subtle; // Change to test prefixed implementations

  // When verifying an Ed25519 or Ed448 signature, if the public key or the first half of the signature (R) is
  // an invalid or small-order element, return false.
  Object.keys(kSmallOrderTestCases).forEach(function (algorithmName) {
      var algorithm = {name: algorithmName};
      kSmallOrderTestCases[algorithmName].forEach(function(test) {
          promise_test(async() => {
              let isVerified = true;
              let publicKey;
              try {
                  publicKey = await subtle.importKey("raw", test.keyData, algorithm, false, ["verify"])
                  isVerified = await subtle.verify(algorithm, publicKey, test.signature, test.message);
              } catch (err) {
                  assert_true(publicKey !== undefined, "Public key should be valid.");
                  assert_unreached("The operation shouldn't fail, but it thown this error: " + err.name + ": " + err.message + ".");
              }
              assert_equals(isVerified, test.verified, "Signature verification result.");
          }, algorithmName + " Verification checks with small-order key of order - Test " + test.id);
      });
  });

  return;
}
