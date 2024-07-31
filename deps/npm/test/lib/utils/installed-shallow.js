const t = require('tap')
const installed = require('../../../lib/utils/installed-shallow.js')
const mockNpm = require('../../fixtures/mock-npm')

const mockShallow = async (t, config) => {
  const res = await mockNpm(t, {
    globalPrefixDir: {
      node_modules: {
        x: {},
        '@scope': {
          y: {},
        },
      },
    },
    prefixDir: {
      node_modules: {
        a: {},
        '@scope': {
          b: {},
        },
      },
    },
    config: { global: false, ...config },
  })
  return res
}

t.test('global not set, include globals with -g', async t => {
  const { npm } = await mockShallow(t)
  const opt = { conf: { argv: { remain: [] } } }
  const res = await installed(npm, opt)
  t.strictSame(res.sort(), [
    '@scope/y -g',
    'x -g',
    'a',
    '@scope/b',
  ].sort())
})

t.test('global set, include globals and not locals', async t => {
  const { npm } = await mockShallow(t, { global: true })
  const opt = { conf: { argv: { remain: [] } } }
  const res = await installed(npm, opt)
  t.strictSame(res.sort(), [
    '@scope/y',
    'x',
  ].sort())
})

t.test('more than 3 items in argv, skip it', async t => {
  const { npm } = await mockShallow(t)
  const opt = { conf: { argv: { remain: [1, 2, 3, 4, 5, 6] } } }
  const res = await installed(npm, opt)
  t.strictSame(res, null)
})
