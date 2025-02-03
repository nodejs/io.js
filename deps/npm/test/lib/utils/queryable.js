const { inspect } = require('node:util')
const t = require('tap')
const Queryable = require('../../../lib/utils/queryable.js')

t.test('retrieve single nested property', async t => {
  const fixture = {
    foo: {
      bar: 'bar',
      baz: 'baz',
    },
    lorem: {
      ipsum: 'ipsum',
    },
  }
  const q = new Queryable(fixture)
  const query = 'foo.bar'
  t.strictSame(q.query(query), { [query]: 'bar' },
    'should retrieve property value when querying for dot-sep name')
})

t.test('query', async t => {
  const fixture = {
    o: 'o',
    single: [
      'item',
    ],
    w: [
      'a',
      'b',
      'c',
    ],
    list: [
      {
        name: 'first',
      },
      {
        name: 'second',
      },
    ],
    foo: {
      bar: 'bar',
      baz: 'baz',
    },
    lorem: {
      ipsum: 'ipsum',
      dolor: [
        'a',
        'b',
        'c',
        {
          sit: [
            'amet',
          ],
        },
      ],
    },
    a: [
      [
        [
          {
            b: [
              [
                {
                  c: 'd',
                },
              ],
            ],
          },
        ],
      ],
    ],
  }
  const q = new Queryable(fixture)
  t.strictSame(
    q.query(['foo.baz', 'lorem.dolor[0]']),
    {
      'foo.baz': 'baz',
      'lorem.dolor[0]': 'a',
    },
    'should retrieve property values when querying for multiple dot-sep names')
  t.strictSame(
    q.query('lorem.dolor[3].sit[0]'),
    {
      'lorem.dolor[3].sit[0]': 'amet',
    },
    'should retrieve property from nested array items')
  t.strictSame(
    q.query('a[0][0][0].b[0][0].c'),
    {
      'a[0][0][0].b[0][0].c': 'd',
    },
    'should retrieve property from deep nested array items')
  t.strictSame(
    q.query('o'),
    {
      o: 'o',
    },
    'should retrieve single level property value')
  t.strictSame(
    q.query('list.name'),
    {
      'list[0].name': 'first',
      'list[1].name': 'second',
    },
    'should automatically expand arrays')
  t.strictSame(
    q.query(['list.name']),
    {
      'list[0].name': 'first',
      'list[1].name': 'second',
    },
    'should automatically expand multiple arrays')
  t.strictSame(
    q.query('w'),
    {
      w: ['a', 'b', 'c'],
    },
    'should return arrays')
  t.strictSame(
    q.query('single'),
    {
      single: 'item',
    },
    'should return single item')
  t.strictSame(
    q.query('missing'),
    undefined,
    'should return undefined')
  t.strictSame(
    q.query('missing[bar]'),
    undefined,
    'should return undefined also')
  t.throws(() => q.query('lorem.dolor[]'),
    { code: 'EINVALIDSYNTAX' },
    'should throw if using empty brackets notation'
  )
  t.throws(() => q.query('lorem.dolor[].sit[0]'),
    { code: 'EINVALIDSYNTAX' },
    'should throw if using nested empty brackets notation'
  )

  const qq = new Queryable({
    foo: {
      bar: 'bar',
    },
  })
  t.strictSame(
    qq.query(''),
    {
      '': {
        foo: {
          bar: 'bar',
        },
      },
    },
    'should return an object with results in an empty key'
  )
})

t.test('missing key', async t => {
  const fixture = {
    foo: {
      bar: 'bar',
    },
  }
  const q = new Queryable(fixture)
  const query = 'foo.missing'
  t.equal(q.query(query), undefined,
    'should retrieve no results')
})

t.test('no data object', async t => {
  t.throws(
    () => new Queryable(),
    { code: 'ENOQUERYABLEOBJ' },
    'should throw ENOQUERYABLEOBJ error'
  )
  t.throws(
    () => new Queryable(1),
    { code: 'ENOQUERYABLEOBJ' },
    'should throw ENOQUERYABLEOBJ error'
  )
})

t.test('get values', async t => {
  const q = new Queryable({
    foo: {
      bar: 'bar',
    },
  })
  t.equal(q.get('foo.bar'), 'bar', 'should retrieve value')
  t.equal(q.get('missing'), undefined, 'should return undefined')
})

t.test('set property values', async t => {
  const fixture = {
    foo: {
      bar: 'bar',
    },
  }
  const q = new Queryable(fixture)
  q.set('foo.baz', 'baz')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
      },
    },
    'should add new property and its assigned value'
  )
  q.set('foo[lorem.ipsum]', 'LOREM IPSUM')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
    },
    'should be able to set square brackets props'
  )
  q.set('a.b[c.d]', 'omg')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
      a: {
        b: {
          'c.d': 'omg',
        },
      },
    },
    'should be able to nest square brackets props'
  )
  q.set('a.b[e][f.g][1.0.0]', 'multiple')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
      a: {
        b: {
          'c.d': 'omg',
          e: {
            'f.g': {
              '1.0.0': 'multiple',
            },
          },
        },
      },
    },
    'should be able to nest multiple square brackets props'
  )
  q.set('a.b[e][f.g][2.0.0].author.name', 'Ruy Adorno')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
      a: {
        b: {
          'c.d': 'omg',
          e: {
            'f.g': {
              '1.0.0': 'multiple',
              '2.0.0': {
                author: {
                  name: 'Ruy Adorno',
                },
              },
            },
          },
        },
      },
    },
    'should be able to use dot-sep notation after square bracket props'
  )
  q.set('a.b[e][f.g][2.0.0].author[url]', 'https://npmjs.com')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
      a: {
        b: {
          'c.d': 'omg',
          e: {
            'f.g': {
              '1.0.0': 'multiple',
              '2.0.0': {
                author: {
                  name: 'Ruy Adorno',
                  url: 'https://npmjs.com',
                },
              },
            },
          },
        },
      },
    },
    'should be able to have multiple, separated, square brackets props'
  )
  q.set('a.b[e][f.g][2.0.0].author[foo][bar].lorem.ipsum[dolor][sit][amet].omg', 'O_O')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: 'bar',
        baz: 'baz',
        'lorem.ipsum': 'LOREM IPSUM',
      },
      a: {
        b: {
          'c.d': 'omg',
          e: {
            'f.g': {
              '1.0.0': 'multiple',
              '2.0.0': {
                author: {
                  name: 'Ruy Adorno',
                  url: 'https://npmjs.com',
                  foo: {
                    bar: {
                      lorem: {
                        ipsum: {
                          dolor: {
                            sit: {
                              amet: {
                                omg: 'O_O',
                              },
                            },
                          },
                        },
                      },
                    },
                  },
                },
              },
            },
          },
        },
      },
    },
    'many many times...'
  )
  t.throws(
    () => q.set('foo.bar.nest', 'should throw'),
    { code: 'EOVERRIDEVALUE' },
    'should throw if trying to override a literal value with an object'
  )
  q.set('foo.bar.nest', 'use the force!', { force: true })
  t.strictSame(
    q.toJSON().foo,
    {
      bar: {
        nest: 'use the force!',
      },
      baz: 'baz',
      'lorem.ipsum': 'LOREM IPSUM',
    },
    'should allow overriding literal values when using force option'
  )

  const qq = new Queryable({})
  qq.set('foo.bar.baz', 'BAZ')
  t.strictSame(
    qq.toJSON(),
    {
      foo: {
        bar: {
          baz: 'BAZ',
        },
      },
    },
    'should add new props to qq object'
  )
  qq.set('foo.bar.bario', 'bario')
  t.strictSame(
    qq.toJSON(),
    {
      foo: {
        bar: {
          baz: 'BAZ',
          bario: 'bario',
        },
      },
    },
    'should add new props to a previously existing object'
  )
  qq.set('lorem', 'lorem')
  t.strictSame(
    qq.toJSON(),
    {
      foo: {
        bar: {
          baz: 'BAZ',
          bario: 'bario',
        },
      },
      lorem: 'lorem',
    },
    'should append new props added to object later'
  )
  qq.set('foo.bar[foo.bar]', 'foo.bar.with.dots')
  t.strictSame(
    qq.toJSON(),
    {
      foo: {
        bar: {
          'foo.bar': 'foo.bar.with.dots',
          baz: 'BAZ',
          bario: 'bario',
        },
      },
      lorem: 'lorem',
    },
    'should append new props added to object later'
  )
})

t.test('set arrays', async t => {
  const q = new Queryable({})

  q.set('foo[1]', 'b')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        undefined,
        'b',
      ],
    },
    'should be able to set items in an array using index references'
  )

  q.set('foo[0]', 'a')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
      ],
    },
    'should be able to set a previously missing item to an array'
  )

  q.set('foo[2]', 'c')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
        'c',
      ],
    },
    'should be able to append more items to an array'
  )

  q.set('foo[2]', 'C')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
        'C',
      ],
    },
    'should be able to override array items'
  )

  t.throws(
    () => q.set('foo[2].bar', 'bar'),
    { code: 'EOVERRIDEVALUE' },
    'should throw if trying to override an array literal item with an obj'
  )

  q.set('foo[2].bar', 'bar', { force: true })
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
        { bar: 'bar' },
      ],
    },
    'should be able to override an array string item with an obj'
  )

  q.set('foo[3].foo', 'surprise surprise, another foo')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
        { bar: 'bar' },
        {
          foo: 'surprise surprise, another foo',
        },
      ],
    },
    'should be able to append more items to an array'
  )

  q.set('foo[3].foo', 'FOO')
  t.strictSame(
    q.toJSON(),
    {
      foo: [
        'a',
        'b',
        { bar: 'bar' },
        {
          foo: 'FOO',
        },
      ],
    },
    'should be able to override property of an obj inside an array'
  )

  const qq = new Queryable({})
  qq.set('foo[0].bar[1].baz.bario[0][0][0]', 'something')
  t.strictSame(
    qq.toJSON(),
    {
      foo: [
        {
          bar: [
            undefined,
            {
              baz: {
                bario: [[['something']]],
              },
            },
          ],
        },
      ],
    },
    'should append as many arrays as necessary'
  )
  qq.set('foo[0].bar[1].baz.bario[0][1][0]', 'something else')
  t.strictSame(
    qq.toJSON(),
    {
      foo: [
        {
          bar: [
            undefined,
            {
              baz: {
                bario: [[
                  ['something'],
                  ['something else'],
                ]],
              },
            },
          ],
        },
      ],
    },
    'should append as many arrays as necessary'
  )
  qq.set('foo', null)
  t.strictSame(
    qq.toJSON(),
    {
      foo: null,
    },
    'should be able to set a value to null'
  )
  qq.set('foo.bar', 'bar')
  t.strictSame(
    qq.toJSON(),
    {
      foo: {
        bar: 'bar',
      },
    },
    'should be able to replace a null value with properties'
  )

  const qqq = new Queryable({
    arr: [
      'a',
      'b',
    ],
  })

  qqq.set('arr[]', 'c')
  t.strictSame(
    qqq.toJSON(),
    {
      arr: [
        'a',
        'b',
        'c',
      ],
    },
    'should be able to append to array using empty bracket notation'
  )

  qqq.set('arr[].foo', 'foo')
  t.strictSame(
    qqq.toJSON(),
    {
      arr: [
        'a',
        'b',
        'c',
        {
          foo: 'foo',
        },
      ],
    },
    'should be able to append objects to array using empty bracket notation'
  )

  qqq.set('arr[].bar.name', 'BAR')
  t.strictSame(
    qqq.toJSON(),
    {
      arr: [
        'a',
        'b',
        'c',
        {
          foo: 'foo',
        },
        {
          bar: {
            name: 'BAR',
          },
        },
      ],
    },
    'should be able to append more objects to array using empty brackets'
  )

  qqq.set('foo.bar.baz[].lorem.ipsum', 'something')
  t.strictSame(
    qqq.toJSON(),
    {
      arr: [
        'a',
        'b',
        'c',
        {
          foo: 'foo',
        },
        {
          bar: {
            name: 'BAR',
          },
        },
      ],
      foo: {
        bar: {
          baz: [
            {
              lorem: {
                ipsum: 'something',
              },
            },
          ],
        },
      },
    },
    'should be able to append to array using empty brackets in nested objs'
  )

  qqq.set('foo.bar.baz[].lorem.array[]', 'new item')
  t.strictSame(
    qqq.toJSON(),
    {
      arr: [
        'a',
        'b',
        'c',
        {
          foo: 'foo',
        },
        {
          bar: {
            name: 'BAR',
          },
        },
      ],
      foo: {
        bar: {
          baz: [
            {
              lorem: {
                ipsum: 'something',
              },
            },
            {
              lorem: {
                array: [
                  'new item',
                ],
              },
            },
          ],
        },
      },
    },
    'should be able to append to array using empty brackets in nested objs'
  )

  const qqqq = new Queryable({
    arr: [
      'a',
      'b',
    ],
  })
  t.throws(
    () => qqqq.set('arr.foo', 'foo'),
    { code: 'ENOADDPROP' },
    'should throw an override error'
  )

  qqqq.set('arr.foo', 'foo', { force: true })
  t.strictSame(
    qqqq.toJSON(),
    {
      arr: {
        0: 'a',
        1: 'b',
        foo: 'foo',
      },
    },
    'should be able to override arrays with objects when using force=true'
  )

  qqqq.set('bar[]', 'item', { force: true })
  t.strictSame(
    qqqq.toJSON(),
    {
      arr: {
        0: 'a',
        1: 'b',
        foo: 'foo',
      },
      bar: [
        'item',
      ],
    },
    'should be able to create new array with item when using force=true'
  )

  qqqq.set('bar[]', 'something else', { force: true })
  t.strictSame(
    qqqq.toJSON(),
    {
      arr: {
        0: 'a',
        1: 'b',
        foo: 'foo',
      },
      bar: [
        'item',
        'something else',
      ],
    },
    'should be able to append items to arrays when using force=true'
  )

  const qqqqq = new Queryable({
    arr: [
      null,
    ],
  })
  qqqqq.set('arr[]', 'b')
  t.strictSame(
    qqqqq.toJSON(),
    {
      arr: [
        null,
        'b',
      ],
    },
    'should be able to append items with empty items'
  )
  qqqqq.set('arr[0]', 'a')
  t.strictSame(
    qqqqq.toJSON(),
    {
      arr: [
        'a',
        'b',
      ],
    },
    'should be able to replace empty items in an array'
  )
  qqqqq.set('lorem.ipsum', 3)
  t.strictSame(
    qqqqq.toJSON(),
    {
      arr: [
        'a',
        'b',
      ],
      lorem: {
        ipsum: 3,
      },
    },
    'should be able to replace empty items in an array'
  )
  t.throws(
    () => qqqqq.set('lorem[]', 4),
    { code: 'ENOAPPEND' },
    'should throw error if using empty square bracket in an non-array item'
  )
  qqqqq.set('lorem[0]', 3)
  t.strictSame(
    qqqqq.toJSON(),
    {
      arr: [
        'a',
        'b',
      ],
      lorem: {
        0: 3,
        ipsum: 3,
      },
    },
    'should be able add indexes as props when finding an object'
  )
  qqqqq.set('lorem.1', 3)
  t.strictSame(
    qqqqq.toJSON(),
    {
      arr: [
        'a',
        'b',
      ],
      lorem: {
        0: 3,
        1: 3,
        ipsum: 3,
      },
    },
    'should be able add numeric props to an obj'
  )
})

t.test('delete values', async t => {
  const q = new Queryable({
    foo: {
      bar: {
        lorem: 'lorem',
      },
    },
  })
  q.delete('foo.bar.lorem')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        bar: {},
      },
    },
    'should delete queried item'
  )
  q.delete('foo')
  t.strictSame(
    q.toJSON(),
    {},
    'should delete nested items'
  )
  q.set('foo.a.b.c[0]', 'value')
  q.delete('foo.a.b.c[0]')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        a: {
          b: {
            c: [],
          },
        },
      },
    },
    'should delete array item'
  )
  // creates an array that has an implicit empty first item
  q.set('foo.a.b.c[1][0].foo.bar[0][0]', 'value')
  q.delete('foo.a.b.c[1]')
  t.strictSame(
    q.toJSON(),
    {
      foo: {
        a: {
          b: {
            c: [null],
          },
        },
      },
    },
    'should delete array item'
  )
})

t.test('logger', async t => {
  const q = new Queryable({})
  q.set('foo.bar[0].baz', 'baz')
  t.strictSame(
    inspect(q, { depth: 10 }),
    inspect({
      foo: {
        bar: [
          {
            baz: 'baz',
          },
        ],
      },
    }, { depth: 10 }),
    'should retrieve expected data'
  )
})

t.test('bracket lovers', async t => {
  const q = new Queryable({})
  q.set('[iLoveBrackets]', 'seriously?')
  t.strictSame(
    q.toJSON(),
    {
      '[iLoveBrackets]': 'seriously?',
    },
    'should be able to set top-level props using square brackets notation'
  )

  t.equal(q.get('[iLoveBrackets]'), 'seriously?',
    'should bypass square bracket in top-level properties')

  q.set('[0]', '-.-')
  t.strictSame(
    q.toJSON(),
    {
      '[iLoveBrackets]': 'seriously?',
      '[0]': '-.-',
    },
    'any top-level item can not be parsed with square bracket notation'
  )
})
