const t = require('tap')
const { join, resolve, basename, extname } = require('node:path')
const fs = require('node:fs/promises')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const docs = require('@npmcli/docs')

const { load: loadMockNpm } = require('../fixtures/mock-npm.js')
const { definitions } = require('@npmcli/config/lib/definitions')
const cmdList = require('../../lib/utils/cmd-list.js')
const pkg = require('../../package.json')
const { cleanCwd } = require('../fixtures/clean-snapshot.js')

t.test('command list', async t => {
  for (const [key, value] of Object.entries(cmdList)) {
    t.matchSnapshot(value, key)
  }
})

t.test('shorthands', async t => {
  t.matchSnapshot(docs.shorthands(docs.TAGS.SHORTHANDS, {}), 'docs')
})

t.test('config', async t => {
  const keys = Object.keys(definitions)
  const flat = Object.entries(definitions).filter(([, d]) => d.flatten).map(([k]) => k)
  const notFlat = keys.filter(k => !flat.includes(k))
  t.matchSnapshot(keys, 'all keys')
  t.matchSnapshot(flat, 'keys that are flattened')
  t.matchSnapshot(notFlat, 'keys that are not flattened')
  t.matchSnapshot(docs.config(docs.TAGS.CONFIG, {}), 'all definitions')
})

t.test('flat options', async t => {
  t.cleanSnapshot = (s) => cleanCwd(s)
    .split(cleanCwd(process.execPath)).join('{NODE}')

  const { npm } = await loadMockNpm(t, {
    command: 'version',
    exec: true,
    npm: ({ other }) => ({
      npmRoot: other,
    }),
    otherDirs: {
      'package.json': JSON.stringify({ version: '1.1.1' }),
    },
    globals: {
      'process.stdout.isTTY': false,
      'process.stderr.isTTY': false,
      'process.env': {
        EDITOR: '{EDITOR}',
        SHELL: '{SHELL}',
      },
      'process.version': '2.2.2',
      'process.platform': '{PLATFORM}',
      'process.arch': '{ARCH}',
    },
    mocks: {
      'ci-info': { isCI: true, name: '{CI}' },
      // currently the config version is read from npmRoot and the Npm
      // constructor version is read from the root package.json file. This
      // snapshot is meant to explicitly show that they are read from different
      // places. In the future we should probably only read it from npmRoot,
      // which would require more changes to ensure nothing depends on it being
      // a static prop on the constructor
      '{ROOT}/package.json': { version: '3.3.3' },
    },
  })

  t.matchSnapshot(npm.flatOptions, 'full flat options object')
})

t.test('basic usage', async t => {
  // snapshot basic usage without commands since all the command snapshots
  // are generated in the following test
  const { npm } = await loadMockNpm(t, {
    mocks: {
      '{LIB}/utils/cmd-list.js': { ...cmdList, commands: [] },
    },
    config: { userconfig: '/some/config/file/.npmrc' },
    globals: { process: { platform: 'posix' } },
  })

  t.cleanSnapshot = str => str
    .replace(npm.npmRoot, '{BASEDIR}')
    .replace(npm.config.get('userconfig'), '{USERCONFIG}')
    .split(pkg.version).join('{VERSION}')

  t.matchSnapshot(npm.usage)
})

t.test('usage', async t => {
  const readdir = async (dir, ext) => {
    const files = await fs.readdir(dir)
    return files.filter(f => extname(f) === ext).map(f => basename(f, ext))
  }

  const fsCommands = await readdir(resolve(__dirname, '../../lib/commands'), '.js')
  const docsCommands = await readdir(join(docs.paths.content, 'commands'), docs.DOC_EXT)
  const bareCommands = ['npm', 'npx']

  // XXX: These extra commands exist as js files but not as docs pages
  const allDocs = docsCommands.concat(['get', 'set', 'll']).map(n => n.replace('npm-', ''))

  // ensure that the list of js files in commands, docs files, and the command list
  // are all in sync. eg, this will error if a command is removed but not its docs file
  t.strictSame(
    fsCommands.sort(localeCompare),
    cmdList.commands,
    'command list and fs are the same'
  )
  t.strictSame(
    allDocs.filter(f => !bareCommands.includes(f)).sort(localeCompare),
    cmdList.commands,
    'command list and docs files are the same'
  )

  // use the list of files from the docs since those include the special cases
  // for the bare npm and npx usage
  for (const cmd of allDocs) {
    t.test(cmd, async t => {
      let output = null
      if (!bareCommands.includes(cmd)) {
        const mock = await loadMockNpm(t, { command: cmd })
        output = mock[cmd].usage
      }

      const usage = docs.usage(docs.TAGS.USAGE, { path: cmd })
      const params = docs.params(docs.TAGS.CONFIG, { path: cmd })
        .split('\n')
        .filter(l => l.startsWith('#### '))
        .join('\n') || 'NO PARAMS'

      t.matchSnapshot([output, usage, params].filter(Boolean).join('\n\n'))
    })
  }
})
