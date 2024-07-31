// META: global=window,dedicatedworker,shadowrealm

const combinations = [
  (t => [t, t.readable])(new TransformStream()),
  (t => [t.readable, t])(new TransformStream()),
  (t => [t, t.writable])(new TransformStream()),
  (t => [t.writable, t])(new TransformStream()),
];

for (const combination of combinations) {
  test(() => {
    assert_throws_dom(
      "DataCloneError",
      () => structuredClone(combination, { transfer: combination }),
      "structuredClone should throw"
    );
  }, `Transferring ${combination} should fail`);
}
