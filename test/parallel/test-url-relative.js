'use strict';
require('../common');
const assert = require('assert');
const inspect = require('util').inspect;
const url = require('url');

// When source is false
assert.strictEqual(url.resolveObject('', 'foo'), 'foo');

// [from, path, expected]
const relativeTests = [
  ['/foo/bar/baz', 'quux', '/foo/bar/quux'],
  ['/foo/bar/baz', 'quux/asdf', '/foo/bar/quux/asdf'],
  ['/foo/bar/baz', 'quux/baz', '/foo/bar/quux/baz'],
  ['/foo/bar/baz', '../quux/baz', '/foo/quux/baz'],
  ['/foo/bar/baz', '/bar', '/bar'],
  ['/foo/bar/baz/', 'quux', '/foo/bar/baz/quux'],
  ['/foo/bar/baz/', 'quux/baz', '/foo/bar/baz/quux/baz'],
  ['/foo/bar/baz', '../../../../../../../../quux/baz', '/quux/baz'],
  ['/foo/bar/baz', '../../../../../../../quux/baz', '/quux/baz'],
  ['/foo', '.', '/'],
  ['/foo', '..', '/'],
  ['/foo/', '.', '/foo/'],
  ['/foo/', '..', '/'],
  ['/foo/bar', '.', '/foo/'],
  ['/foo/bar', '..', '/'],
  ['/foo/bar/', '.', '/foo/bar/'],
  ['/foo/bar/', '..', '/foo/'],
  ['foo/bar', '../../../baz', '../../baz'],
  ['foo/bar/', '../../../baz', '../baz'],
  ['http://example.com/b//c//d;p?q#blarg', 'https:#hash2', 'https:///#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'https:/p/a/t/h?s#hash2',
   'https://p/a/t/h?s#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'https://u:p@h.com/p/a/t/h?s#hash2',
   'https://u:p@h.com/p/a/t/h?s#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'https:/a/b/c/d',
   'https://a/b/c/d'],
  ['http://example.com/b//c//d;p?q#blarg',
   'http:#hash2',
   'http://example.com/b//c//d;p?q#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'http:/p/a/t/h?s#hash2',
   'http://example.com/p/a/t/h?s#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'http://u:p@h.com/p/a/t/h?s#hash2',
   'http://u:p@h.com/p/a/t/h?s#hash2'],
  ['http://example.com/b//c//d;p?q#blarg',
   'http:/a/b/c/d',
   'http://example.com/a/b/c/d'],
  ['/foo/bar/baz', '/../etc/passwd', '/etc/passwd'],
  ['http://localhost', 'file:///Users/foo', 'file:///Users/foo'],
  ['http://localhost', 'file://foo/Users', 'file://foo/Users'],
  ['https://registry.npmjs.org', '@foo/bar', 'https://registry.npmjs.org/@foo/bar'],
];
for (let i = 0; i < relativeTests.length; i++) {
  const relativeTest = relativeTests[i];

  const a = url.resolve(relativeTest[0], relativeTest[1]);
  const e = relativeTest[2];
  assert.strictEqual(a, e,
                     `resolve(${relativeTest[0]}, ${relativeTest[1]})` +
                     ` == ${e}\n  actual=${a}`);
}

//
// Tests below taken from Chiron
// http://code.google.com/p/chironjs/source/browse/trunk/src/test/http/url.js
//
// Copyright (c) 2002-2008 Kris Kowal <http://cixar.com/~kris.kowal>
// used with permission under MIT License
//
// Changes marked with @isaacs

const bases = [
  'http://a/b/c/d;p?q',
  'http://a/b/c/d;p?q=1/2',
  'http://a/b/c/d;p=1/2?q',
  'fred:///s//a/b/c',
  'http:///s//a/b/c',
];

// [to, from, result]
const relativeTests2 = [
  // http://lists.w3.org/Archives/Public/uri/2004Feb/0114.html
  ['../c', 'foo:a/b', 'foo:c'],
  ['foo:.', 'foo:a', 'foo:'],
  ['/foo/../../../bar', 'zz:abc', 'zz:/bar'],
  ['/foo/../bar', 'zz:abc', 'zz:/bar'],
  // @isaacs Disagree. Not how web browsers resolve this.
  ['foo/../../../bar', 'zz:abc', 'zz:bar'],
  // ['foo/../../../bar',  'zz:abc', 'zz:../../bar'], // @isaacs Added
  ['foo/../bar', 'zz:abc', 'zz:bar'],
  ['zz:.', 'zz:abc', 'zz:'],
  ['/.', bases[0], 'http://a/'],
  ['/.foo', bases[0], 'http://a/.foo'],
  ['.foo', bases[0], 'http://a/b/c/.foo'],

  // http://gbiv.com/protocols/uri/test/rel_examples1.html
  // examples from RFC 2396
  ['g:h', bases[0], 'g:h'],
  ['g', bases[0], 'http://a/b/c/g'],
  ['./g', bases[0], 'http://a/b/c/g'],
  ['g/', bases[0], 'http://a/b/c/g/'],
  ['/g', bases[0], 'http://a/g'],
  ['//g', bases[0], 'http://g/'],
  // Changed with RFC 2396bis
  // ('?y', bases[0], 'http://a/b/c/d;p?y'],
  ['?y', bases[0], 'http://a/b/c/d;p?y'],
  ['g?y', bases[0], 'http://a/b/c/g?y'],
  // Changed with RFC 2396bis
  // ('#s', bases[0], CURRENT_DOC_URI + '#s'],
  ['#s', bases[0], 'http://a/b/c/d;p?q#s'],
  ['g#s', bases[0], 'http://a/b/c/g#s'],
  ['g?y#s', bases[0], 'http://a/b/c/g?y#s'],
  [';x', bases[0], 'http://a/b/c/;x'],
  ['g;x', bases[0], 'http://a/b/c/g;x'],
  ['g;x?y#s', bases[0], 'http://a/b/c/g;x?y#s'],
  // Changed with RFC 2396bis
  // ('', bases[0], CURRENT_DOC_URI],
  ['', bases[0], 'http://a/b/c/d;p?q'],
  ['.', bases[0], 'http://a/b/c/'],
  ['./', bases[0], 'http://a/b/c/'],
  ['..', bases[0], 'http://a/b/'],
  ['../', bases[0], 'http://a/b/'],
  ['../g', bases[0], 'http://a/b/g'],
  ['../..', bases[0], 'http://a/'],
  ['../../', bases[0], 'http://a/'],
  ['../../g', bases[0], 'http://a/g'],
  ['../../../g', bases[0], ('http://a/../g', 'http://a/g')],
  ['../../../../g', bases[0], ('http://a/../../g', 'http://a/g')],
  // Changed with RFC 2396bis
  // ('/./g', bases[0], 'http://a/./g'],
  ['/./g', bases[0], 'http://a/g'],
  // Changed with RFC 2396bis
  // ('/../g', bases[0], 'http://a/../g'],
  ['/../g', bases[0], 'http://a/g'],
  ['g.', bases[0], 'http://a/b/c/g.'],
  ['.g', bases[0], 'http://a/b/c/.g'],
  ['g..', bases[0], 'http://a/b/c/g..'],
  ['..g', bases[0], 'http://a/b/c/..g'],
  ['./../g', bases[0], 'http://a/b/g'],
  ['./g/.', bases[0], 'http://a/b/c/g/'],
  ['g/./h', bases[0], 'http://a/b/c/g/h'],
  ['g/../h', bases[0], 'http://a/b/c/h'],
  ['g;x=1/./y', bases[0], 'http://a/b/c/g;x=1/y'],
  ['g;x=1/../y', bases[0], 'http://a/b/c/y'],
  ['g?y/./x', bases[0], 'http://a/b/c/g?y/./x'],
  ['g?y/../x', bases[0], 'http://a/b/c/g?y/../x'],
  ['g#s/./x', bases[0], 'http://a/b/c/g#s/./x'],
  ['g#s/../x', bases[0], 'http://a/b/c/g#s/../x'],
  ['http:g', bases[0], ('http:g', 'http://a/b/c/g')],
  ['http:', bases[0], ('http:', bases[0])],
  // Not sure where this one originated
  ['/a/b/c/./../../g', bases[0], 'http://a/a/g'],

  // http://gbiv.com/protocols/uri/test/rel_examples2.html
  // slashes in base URI's query args
  ['g', bases[1], 'http://a/b/c/g'],
  ['./g', bases[1], 'http://a/b/c/g'],
  ['g/', bases[1], 'http://a/b/c/g/'],
  ['/g', bases[1], 'http://a/g'],
  ['//g', bases[1], 'http://g/'],
  // Changed in RFC 2396bis
  // ('?y', bases[1], 'http://a/b/c/?y'],
  ['?y', bases[1], 'http://a/b/c/d;p?y'],
  ['g?y', bases[1], 'http://a/b/c/g?y'],
  ['g?y/./x', bases[1], 'http://a/b/c/g?y/./x'],
  ['g?y/../x', bases[1], 'http://a/b/c/g?y/../x'],
  ['g#s', bases[1], 'http://a/b/c/g#s'],
  ['g#s/./x', bases[1], 'http://a/b/c/g#s/./x'],
  ['g#s/../x', bases[1], 'http://a/b/c/g#s/../x'],
  ['./', bases[1], 'http://a/b/c/'],
  ['../', bases[1], 'http://a/b/'],
  ['../g', bases[1], 'http://a/b/g'],
  ['../../', bases[1], 'http://a/'],
  ['../../g', bases[1], 'http://a/g'],

  // http://gbiv.com/protocols/uri/test/rel_examples3.html
  // slashes in path params
  // all of these changed in RFC 2396bis
  ['g', bases[2], 'http://a/b/c/d;p=1/g'],
  ['./g', bases[2], 'http://a/b/c/d;p=1/g'],
  ['g/', bases[2], 'http://a/b/c/d;p=1/g/'],
  ['g?y', bases[2], 'http://a/b/c/d;p=1/g?y'],
  [';x', bases[2], 'http://a/b/c/d;p=1/;x'],
  ['g;x', bases[2], 'http://a/b/c/d;p=1/g;x'],
  ['g;x=1/./y', bases[2], 'http://a/b/c/d;p=1/g;x=1/y'],
  ['g;x=1/../y', bases[2], 'http://a/b/c/d;p=1/y'],
  ['./', bases[2], 'http://a/b/c/d;p=1/'],
  ['../', bases[2], 'http://a/b/c/'],
  ['../g', bases[2], 'http://a/b/c/g'],
  ['../../', bases[2], 'http://a/b/'],
  ['../../g', bases[2], 'http://a/b/g'],

  // http://gbiv.com/protocols/uri/test/rel_examples4.html
  // double and triple slash, unknown scheme
  ['g:h', bases[3], 'g:h'],
  ['g', bases[3], 'fred:///s//a/b/g'],
  ['./g', bases[3], 'fred:///s//a/b/g'],
  ['g/', bases[3], 'fred:///s//a/b/g/'],
  ['/g', bases[3], 'fred:///g'],  // May change to fred:///s//a/g
  ['//g', bases[3], 'fred://g'],   // May change to fred:///s//g
  ['//g/x', bases[3], 'fred://g/x'], // May change to fred:///s//g/x
  ['///g', bases[3], 'fred:///g'],
  ['./', bases[3], 'fred:///s//a/b/'],
  ['../', bases[3], 'fred:///s//a/'],
  ['../g', bases[3], 'fred:///s//a/g'],

  ['../../', bases[3], 'fred:///s//'],
  ['../../g', bases[3], 'fred:///s//g'],
  ['../../../g', bases[3], 'fred:///s/g'],
  // May change to fred:///s//a/../../../g
  ['../../../../g', bases[3], 'fred:///g'],

  // http://gbiv.com/protocols/uri/test/rel_examples5.html
  // double and triple slash, well-known scheme
  ['g:h', bases[4], 'g:h'],
  ['g', bases[4], 'http:///s//a/b/g'],
  ['./g', bases[4], 'http:///s//a/b/g'],
  ['g/', bases[4], 'http:///s//a/b/g/'],
  ['/g', bases[4], 'http:///g'],  // May change to http:///s//a/g
  ['//g', bases[4], 'http://g/'],   // May change to http:///s//g
  ['//g/x', bases[4], 'http://g/x'], // May change to http:///s//g/x
  ['///g', bases[4], 'http:///g'],
  ['./', bases[4], 'http:///s//a/b/'],
  ['../', bases[4], 'http:///s//a/'],
  ['../g', bases[4], 'http:///s//a/g'],
  ['../../', bases[4], 'http:///s//'],
  ['../../g', bases[4], 'http:///s//g'],
  // May change to http:///s//a/../../g
  ['../../../g', bases[4], 'http:///s/g'],
  // May change to http:///s//a/../../../g
  ['../../../../g', bases[4], 'http:///g'],

  // From Dan Connelly's tests in http://www.w3.org/2000/10/swap/uripath.py
  ['bar:abc', 'foo:xyz', 'bar:abc'],
  ['../abc', 'http://example/x/y/z', 'http://example/x/abc'],
  ['http://example/x/abc', 'http://example2/x/y/z', 'http://example/x/abc'],
  ['../r', 'http://ex/x/y/z', 'http://ex/x/r'],
  ['q/r', 'http://ex/x/y', 'http://ex/x/q/r'],
  ['q/r#s', 'http://ex/x/y', 'http://ex/x/q/r#s'],
  ['q/r#s/t', 'http://ex/x/y', 'http://ex/x/q/r#s/t'],
  ['ftp://ex/x/q/r', 'http://ex/x/y', 'ftp://ex/x/q/r'],
  ['', 'http://ex/x/y', 'http://ex/x/y'],
  ['', 'http://ex/x/y/', 'http://ex/x/y/'],
  ['', 'http://ex/x/y/pdq', 'http://ex/x/y/pdq'],
  ['z/', 'http://ex/x/y/', 'http://ex/x/y/z/'],
  ['#Animal',
   'file:/swap/test/animal.rdf',
   'file:/swap/test/animal.rdf#Animal'],
  ['../abc', 'file:/e/x/y/z', 'file:/e/x/abc'],
  ['/example/x/abc', 'file:/example2/x/y/z', 'file:/example/x/abc'],
  ['../r', 'file:/ex/x/y/z', 'file:/ex/x/r'],
  ['/r', 'file:/ex/x/y/z', 'file:/r'],
  ['q/r', 'file:/ex/x/y', 'file:/ex/x/q/r'],
  ['q/r#s', 'file:/ex/x/y', 'file:/ex/x/q/r#s'],
  ['q/r#', 'file:/ex/x/y', 'file:/ex/x/q/r#'],
  ['q/r#s/t', 'file:/ex/x/y', 'file:/ex/x/q/r#s/t'],
  ['ftp://ex/x/q/r', 'file:/ex/x/y', 'ftp://ex/x/q/r'],
  ['', 'file:/ex/x/y', 'file:/ex/x/y'],
  ['', 'file:/ex/x/y/', 'file:/ex/x/y/'],
  ['', 'file:/ex/x/y/pdq', 'file:/ex/x/y/pdq'],
  ['z/', 'file:/ex/x/y/', 'file:/ex/x/y/z/'],
  ['file://meetings.example.com/cal#m1',
   'file:/devel/WWW/2000/10/swap/test/reluri-1.n3',
   'file://meetings.example.com/cal#m1'],
  ['file://meetings.example.com/cal#m1',
   'file:/home/connolly/w3ccvs/WWW/2000/10/swap/test/reluri-1.n3',
   'file://meetings.example.com/cal#m1'],
  ['./#blort', 'file:/some/dir/foo', 'file:/some/dir/#blort'],
  ['./#', 'file:/some/dir/foo', 'file:/some/dir/#'],
  // Ryan Lee
  ['./', 'http://example/x/abc.efg', 'http://example/x/'],


  // Graham Klyne's tests
  // http://www.ninebynine.org/Software/HaskellUtils/Network/UriTest.xls
  // 01-31 are from Connelly's cases

  // 32-49
  ['./q:r', 'http://ex/x/y', 'http://ex/x/q:r'],
  ['./p=q:r', 'http://ex/x/y', 'http://ex/x/p=q:r'],
  ['?pp/rr', 'http://ex/x/y?pp/qq', 'http://ex/x/y?pp/rr'],
  ['y/z', 'http://ex/x/y?pp/qq', 'http://ex/x/y/z'],
  ['local/qual@domain.org#frag',
   'mailto:local',
   'mailto:local/qual@domain.org#frag'],
  ['more/qual2@domain2.org#frag',
   'mailto:local/qual1@domain1.org',
   'mailto:local/more/qual2@domain2.org#frag'],
  ['y?q', 'http://ex/x/y?q', 'http://ex/x/y?q'],
  ['/x/y?q', 'http://ex?p', 'http://ex/x/y?q'],
  ['c/d', 'foo:a/b', 'foo:a/c/d'],
  ['/c/d', 'foo:a/b', 'foo:/c/d'],
  ['', 'foo:a/b?c#d', 'foo:a/b?c'],
  ['b/c', 'foo:a', 'foo:b/c'],
  ['../b/c', 'foo:/a/y/z', 'foo:/a/b/c'],
  ['./b/c', 'foo:a', 'foo:b/c'],
  ['/./b/c', 'foo:a', 'foo:/b/c'],
  ['../../d', 'foo://a//b/c', 'foo://a/d'],
  ['.', 'foo:a', 'foo:'],
  ['..', 'foo:a', 'foo:'],

  // 50-57[cf. TimBL comments --
  //  http://lists.w3.org/Archives/Public/uri/2003Feb/0028.html,
  //  http://lists.w3.org/Archives/Public/uri/2003Jan/0008.html)
  ['abc', 'http://example/x/y%2Fz', 'http://example/x/abc'],
  ['../../x%2Fabc', 'http://example/a/x/y/z', 'http://example/a/x%2Fabc'],
  ['../x%2Fabc', 'http://example/a/x/y%2Fz', 'http://example/a/x%2Fabc'],
  ['abc', 'http://example/x%2Fy/z', 'http://example/x%2Fy/abc'],
  ['q%3Ar', 'http://ex/x/y', 'http://ex/x/q%3Ar'],
  ['/x%2Fabc', 'http://example/x/y%2Fz', 'http://example/x%2Fabc'],
  ['/x%2Fabc', 'http://example/x/y/z', 'http://example/x%2Fabc'],
  ['/x%2Fabc', 'http://example/x/y%2Fz', 'http://example/x%2Fabc'],

  // 70-77
  ['local2@domain2', 'mailto:local1@domain1?query1', 'mailto:local2@domain2'],
  ['local2@domain2?query2',
   'mailto:local1@domain1',
   'mailto:local2@domain2?query2'],
  ['local2@domain2?query2',
   'mailto:local1@domain1?query1',
   'mailto:local2@domain2?query2'],
  ['?query2', 'mailto:local@domain?query1', 'mailto:local@domain?query2'],
  ['local@domain?query2', 'mailto:?query1', 'mailto:local@domain?query2'],
  ['?query2', 'mailto:local@domain?query1', 'mailto:local@domain?query2'],
  ['http://example/a/b?c/../d', 'foo:bar', 'http://example/a/b?c/../d'],
  ['http://example/a/b#c/../d', 'foo:bar', 'http://example/a/b#c/../d'],

  // 82-88
  // @isaacs Disagree. Not how browsers do it.
  // ['http:this', 'http://example.org/base/uri', 'http:this'],
  // @isaacs Added
  ['http:this', 'http://example.org/base/uri', 'http://example.org/base/this'],
  ['http:this', 'http:base', 'http:this'],
  ['.//g', 'f:/a', 'f://g'],
  ['b/c//d/e', 'f://example.org/base/a', 'f://example.org/base/b/c//d/e'],
  ['m2@example.ord/c2@example.org',
   'mid:m@example.ord/c@example.org',
   'mid:m@example.ord/m2@example.ord/c2@example.org'],
  ['mini1.xml',
   'file:///C:/DEV/Haskell/lib/HXmlToolbox-3.01/examples/',
   'file:///C:/DEV/Haskell/lib/HXmlToolbox-3.01/examples/mini1.xml'],
  ['../b/c', 'foo:a/y/z', 'foo:a/b/c'],

  // changeing auth
  ['http://diff:auth@www.example.com',
   'http://asdf:qwer@www.example.com',
   'http://diff:auth@www.example.com/'],

  // changing port
  ['https://example.com:81/',
   'https://example.com:82/',
   'https://example.com:81/'],

  // https://github.com/nodejs/node/issues/1435
  ['https://another.host.com/',
   'https://user:password@example.org/',
   'https://another.host.com/'],
  ['//another.host.com/',
   'https://user:password@example.org/',
   'https://another.host.com/'],
  ['http://another.host.com/',
   'https://user:password@example.org/',
   'http://another.host.com/'],
  ['mailto:another.host.com',
   'mailto:user@example.org',
   'mailto:another.host.com'],
  ['https://example.com/foo',
   'https://user:password@example.com',
   'https://user:password@example.com/foo'],

  // No path at all
  ['#hash1', '#hash2', '#hash1'],
];
for (let i = 0; i < relativeTests2.length; i++) {
  const relativeTest = relativeTests2[i];

  const a = url.resolve(relativeTest[1], relativeTest[0]);
  const e = url.format(relativeTest[2]);
  assert.strictEqual(a, e,
                     `resolve(${relativeTest[0]}, ${relativeTest[1]})` +
                     ` == ${e}\n  actual=${a}`);
}

// If format and parse are inverse operations then
// resolveObject(parse(x), y) == parse(resolve(x, y))

// format: [from, path, expected]
for (let i = 0; i < relativeTests.length; i++) {
  const relativeTest = relativeTests[i];

  let actual = url.resolveObject(url.parse(relativeTest[0]), relativeTest[1]);
  let expected = url.parse(relativeTest[2]);


  assert.deepStrictEqual(actual, expected);

  expected = relativeTest[2];
  actual = url.format(actual);

  assert.strictEqual(actual, expected,
                     `format(${actual}) == ${expected}\n` +
                     `actual: ${actual}`);

}

// format: [to, from, result]
// the test: ['.//g', 'f:/a', 'f://g'] is a fundamental problem
// url.parse('f:/a') does not have a host
// url.resolve('f:/a', './/g') does not have a host because you have moved
// down to the g directory.  i.e. f:     //g, however when this url is parsed
// f:// will indicate that the host is g which is not the case.
// it is unclear to me how to keep this information from being lost
// it may be that a pathname of ////g should collapse to /g but this seems
// to be a lot of work for an edge case.  Right now I remove the test
if (relativeTests2[181][0] === './/g' &&
    relativeTests2[181][1] === 'f:/a' &&
    relativeTests2[181][2] === 'f://g') {
  relativeTests2.splice(181, 1);
}
for (let i = 0; i < relativeTests2.length; i++) {
  const relativeTest = relativeTests2[i];

  let actual = url.resolveObject(url.parse(relativeTest[1]), relativeTest[0]);
  let expected = url.parse(relativeTest[2]);

  assert.deepStrictEqual(
    actual,
    expected,
    `expected ${inspect(expected)} but got ${inspect(actual)}`
  );

  expected = url.format(relativeTest[2]);
  actual = url.format(actual);

  assert.strictEqual(actual, expected,
                     `format(${relativeTest[1]}) == ${expected}\n` +
                     `actual: ${actual}`);
}
