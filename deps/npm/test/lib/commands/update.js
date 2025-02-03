const t = require('tap')
const _mockNpm = require('../../fixtures/mock-npm')

// XXX: this test has been refactored to use the new mockNpm
// but it still only asserts the options passed to arborist.
// TODO: make this really test npm update scenarios
const mockUpdate = async (t, { exec = [], ...opts } = {}) => {
  let ctor = null
  let reify = null
  let finish = null

  const res = await _mockNpm(t, {
    ...opts,
    mocks: {
      '@npmcli/arborist': class Arborist {
        constructor (o) {
          ctor = o
        }

        reify (o) {
          reify = o
        }
      },
      '{LIB}/utils/reify-finish.js': (_, o) => {
        finish = o
      },
    },
  })

  await res.npm.exec('update', exec)

  return {
    ...res,
    ctor,
    reify,
    finish,
  }
}

t.test('no args', async t => {
  const { ctor, reify, finish, prefix } = await mockUpdate(t)

  t.equal(ctor.path, prefix, 'path')
  t.equal(ctor.save, false, 'should default to save=false')
  t.equal(ctor.workspaces, undefined, 'workspaces')

  t.equal(reify.update, true, 'should update all deps')

  t.equal(finish.constructor.name, 'Arborist')
})

t.test('with args', async t => {
  const { ctor, reify } = await mockUpdate(t, {
    config: { save: true },
    exec: ['ipt'],
  })

  t.equal(ctor.save, true, 'save')
  t.strictSame(reify.update, ['ipt'], 'ipt')
})

t.test('update --depth=<number>', async t => {
  const { logs } = await mockUpdate(t, {
    config: { depth: 1 },
  })

  t.match(
    logs.warn.byTitle('update')[0],
    /The --depth option no longer has any effect/,
    'should print expected warning message'
  )
})

t.test('update --global', async t => {
  const { ctor, globalPrefix } = await mockUpdate(t, {
    config: { global: true },
  })

  t.match(ctor.path, globalPrefix)
  t.ok(ctor.path.startsWith(globalPrefix))
})
