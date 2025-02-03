const t = require('tap')
const fs = require('node:fs/promises')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')
const { join } = require('node:path')

t.test('token logout - user config', async t => {
  const { npm, home, logs } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': [
        '//registry.npmjs.org/:_authToken=@foo/',
        'other-config=true',
      ].join('\n'),
    },
  })

  const mockRegistry = new MockRegistry({ tap: t, registry: 'https://registry.npmjs.org/' })
  mockRegistry.logout('@foo/')
  await npm.exec('logout', [])
  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing token for https://registry.npmjs.org/',
    'should log message with correct registry'
  )
  const userRc = await fs.readFile(join(home, '.npmrc'), 'utf-8')
  t.equal(userRc.trim(), 'other-config=true')
})

t.test('token scoped logout - user config', async t => {
  const { npm, home, logs } = await loadMockNpm(t, {
    config: {
      scope: '@myscope',
    },
    homeDir: {
      '.npmrc': [
        '//diff-registry.npmjs.com/:_authToken=@bar/',
        '//registry.npmjs.org/:_authToken=@foo/',
        '@myscope:registry=https://diff-registry.npmjs.com/',

      ].join('\n'),
    },
  })

  const mockRegistry = new MockRegistry({ tap: t, registry: 'https://diff-registry.npmjs.com/' })
  mockRegistry.logout('@bar/')
  await npm.exec('logout', [])
  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing token for https://diff-registry.npmjs.com/',
    'should log message with correct registry'
  )

  const userRc = await fs.readFile(join(home, '.npmrc'), 'utf-8')
  t.equal(userRc.trim(), '//registry.npmjs.org/:_authToken=@foo/')
})

t.test('user/pass logout - user config', async t => {
  const { npm, home, logs } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': [
        '//registry.npmjs.org/:username=foo',
        '//registry.npmjs.org/:_password=bar',
        'other-config=true',
      ].join('\n'),
    },
  })

  await npm.exec('logout', [])
  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing user credentials for https://registry.npmjs.org/',
    'should log message with correct registry'
  )

  const userRc = await fs.readFile(join(home, '.npmrc'), 'utf-8')
  t.equal(userRc.trim(), 'other-config=true')
})

t.test('missing credentials', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('logout', []),
    {
      code: 'ENEEDAUTH',
      message: /not logged in to https:\/\/registry.npmjs.org\/, so can't log out!/,
    },
    'should reject with expected error code'
  )
})

t.test('ignore invalid scoped registry config', async t => {
  const { npm, home, logs } = await loadMockNpm(t, {
    config: { scope: '@myscope' },
    homeDir: {
      '.npmrc': [
        '//registry.npmjs.org/:_authToken=@foo/',
        'other-config=true',

      ].join('\n'),
    },
  })

  const mockRegistry = new MockRegistry({ tap: t, registry: 'https://registry.npmjs.org/' })
  mockRegistry.logout('@foo/')
  await npm.exec('logout', [])

  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing token for https://registry.npmjs.org/',
    'should log message with correct registry'
  )
  const userRc = await fs.readFile(join(home, '.npmrc'), 'utf-8')
  t.equal(userRc.trim(), 'other-config=true')
})

t.test('token logout - project config', async t => {
  const { npm, home, logs, prefix } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': [
        '//registry.npmjs.org/:_authToken=@foo/',
        'other-config=true',
      ].join('\n'),
    },
    prefixDir: {
      '.npmrc': [
        '//registry.npmjs.org/:_authToken=@bar/',
        'other-config=true',
      ].join('\n'),
    },
  })

  const mockRegistry = new MockRegistry({ tap: t, registry: 'https://registry.npmjs.org/' })
  mockRegistry.logout('@bar/')
  await npm.exec('logout', [])

  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing token for https://registry.npmjs.org/',
    'should log message with correct registry'
  )
  const userRc = await fs.readFile(join(home, '.npmrc'), 'utf-8')
  t.equal(userRc.trim(), [
    '//registry.npmjs.org/:_authToken=@foo/',
    'other-config=true',
  ].join('\n'), 'leaves user config alone')
  t.equal(
    logs.verbose.byTitle('logout')[0],
    'logout clearing token for https://registry.npmjs.org/',
    'should log message with correct registry'
  )
  const projectRc = await fs.readFile(join(prefix, '.npmrc'), 'utf-8')
  t.equal(projectRc.trim(), 'other-config=true', 'removes project config')
})
