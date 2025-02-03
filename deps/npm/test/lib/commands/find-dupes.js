const t = require('tap')
const path = require('node:path')
const fs = require('node:fs')

const { load: loadMockNpm } = require('../../fixtures/mock-npm')
const MockRegistry = require('@npmcli/mock-registry')

const treeWithDupes = {
  'package.json': JSON.stringify({
    name: 'test-top',
    version: '1.0.0',
    dependencies: {
      'test-dep-a': '*',
      'test-dep-b': '*',
    },
  }),
  node_modules: {
    'test-dep-a': {
      'package.json': JSON.stringify({
        name: 'test-dep-a',
        version: '1.0.1',
        dependencies: { 'test-sub': '*' },
      }),
      node_modules: {
        'test-sub': {
          'package.json': JSON.stringify({
            name: 'test-sub',
            version: '1.0.0',
          }),
        },
      },
    },
    'test-dep-b': {
      'package.json': JSON.stringify({
        name: 'test-dep-b',
        version: '1.0.0',
        dependencies: { 'test-sub': '*' },
      }),
      node_modules: {
        'test-sub': {
          'package.json': JSON.stringify({
            name: 'test-sub',
            version: '1.0.0',
          }),
        },
      },
    },
  },
}

t.test('should run dedupe in dryRun mode', async (t) => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: treeWithDupes,
    config: {
      // explicitly set to false so we can be 100% sure it's always true when it
      // hits arborist
      'dry-run': false,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })

  const manifestSub = registry.manifest({
    name: 'test-sub',
    manifests: [{ name: 'test-sub', version: '1.0.0' }],
  })

  await registry.package({ manifest: manifestSub })
  await npm.exec('find-dupes', [])
  t.match(joinedOutput(), /added 1 package, and removed 2 packages/)
  t.notOk(
    fs.existsSync(path.join(npm.prefix, 'node_modules', 'test-sub')),
    'test-sub was not hoisted'
  )
  t.ok(
    fs.existsSync(path.join(npm.prefix, 'node_modules', 'test-dep-a', 'node_modules', 'test-sub')),
    'test-dep-a/test-sub was not removed'
  )
  t.ok(
    fs.existsSync(path.join(npm.prefix, 'node_modules', 'test-dep-b', 'node_modules', 'test-sub')),
    'test-dep-b/test-sub was not removed')
})
