const t = require('tap')
const fs = require('node:fs/promises')
const mockNpm = require('../../fixtures/mock-npm')
const { join } = require('node:path')
const { cleanNewlines } = require('../../fixtures/clean-snapshot')

t.test('no args', async t => {
  const { npm } = await mockNpm(t)
  t.rejects(npm.exec('set', []), /Usage:/, 'prints usage')
})

t.test('test-config-item', async t => {
  const { npm, home, joinedOutput } = await mockNpm(t, {
    homeDir: {
      '.npmrc': 'original-config-test=original value',
    },
  })

  t.equal(
    npm.config.get('original-config-test'),
    'original value',
    'original config is set from npmrc'
  )

  t.not(
    npm.config.get('fund'),
    false,
    'config is not already new value'
  )

  await npm.exec('set', ['fund=true'])
  t.equal(joinedOutput(), '', 'outputs nothing')

  t.equal(
    npm.config.get('fund'),
    true,
    'config is set to new value'
  )

  t.equal(
    cleanNewlines(await fs.readFile(join(home, '.npmrc'), 'utf-8')),
    [
      'original-config-test=original value',
      'fund=true',
      '',
    ].join('\n'),
    'npmrc is written with new value'
  )
})
