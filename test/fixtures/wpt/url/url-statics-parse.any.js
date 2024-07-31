// This intentionally does not use resources/urltestdata.json to preserve resources.
[
  {
    "url": undefined,
    "base": undefined,
    "expected": false
  },
  {
    "url": "aaa:b",
    "base": undefined,
    "expected": true
  },
  {
    "url": undefined,
    "base": "aaa:b",
    "expected": false
  },
  {
    "url": "aaa:/b",
    "base": undefined,
    "expected": true
  },
  {
    "url": undefined,
    "base": "aaa:/b",
    "expected": true
  },
  {
    "url": "https://test:test",
    "base": undefined,
    "expected": false
  },
  {
    "url": "a",
    "base": "https://b/",
    "expected": true
  }
].forEach(({ url, base, expected }) => {
  test(() => {
    if (expected == false) {
      assert_equals(URL.parse(url, base), null);
    } else {
      assert_equals(URL.parse(url, base).href, new URL(url, base).href);
    }
  }, `URL.parse(${url}, ${base})`);
});

test(() => {
  assert_not_equals(URL.parse("https://example/"), URL.parse("https://example/"));
}, `URL.parse() should return a unique object`);
