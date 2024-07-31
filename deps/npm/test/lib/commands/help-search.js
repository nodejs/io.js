const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')

/* eslint-disable max-len */
const docsFixtures = {
  dir1: {
    'npm-exec.md': 'the exec command\nhelp has multiple lines of exec help\none of them references exec',
  },
  dir2: {
    'npm-something.md': 'another\ncommand you run\nthat\nreferences exec\nand has multiple lines\nwith no matches\nthat will be ignored\nand another line\nthat does have exec as well',
    'npm-run-script.md': 'the scripted run-script command runs scripts\nand has lines\nsome of which dont match the string run\nor script\nscript',
    'npm-install.md': 'does a thing in a script\nif a thing does not exist in a thing you run\nto install it and run it maybe in a script',
  },
  dir3: {
    'npm-help.md': 'will run the `help-search` command if you need to run it to help you search',
    'npm-help-search.md': 'is the help search command\nthat you get if you run help-search',
    'npm-useless.md': 'exec\nexec',
    'npm-more-useless.md': 'exec exec',
    'npm-extra-useless.md': 'exec\nexec\nexec',
  },
}
/* eslint-enable max-len */

const execHelpSearch = async (t, exec = [], opts) => {
  const { npm, ...rest } = await loadMockNpm(t, {
    npm: ({ other }) => ({ npmRoot: other }),
    // docs/content is hardcoded into the glob path in the command
    otherDirs: {
      docs: {
        content: docsFixtures,
      },
    },
    ...opts,
  })

  await npm.exec('help-search', exec)

  return { npm, output: rest.joinedOutput(), ...rest }
}

t.test('npm help-search', async t => {
  const { output } = await execHelpSearch(t, ['exec'])

  t.match(output, /Top hits for "exec"/, 'outputs results')
})

t.test('npm help-search multiple terms', async t => {
  const { output } = await execHelpSearch(t, ['run', 'script'])

  t.match(output, /Top hits for/, 'outputs results')
  t.match(output, /run:\d+ script:\d+/, 'shows hit counts for both terms')
})

t.test('npm help-search long output', async t => {
  const { output } = await execHelpSearch(t, ['exec'], {
    config: {
      long: true,
    },
  })

  t.match(output, /has multiple lines of exec help/, 'outputs detailed results')
})

t.test('npm help-search long output with color', async t => {
  const { output } = await execHelpSearch(t, ['help-search'], {
    config: {
      long: true,
      color: 'always',
    },
  })

  const chalk = await import('chalk').then(v => v.default)

  const highlightedText = chalk.blue('help-search')
  t.equal(
    output.split('\n').some(line => line.includes(highlightedText)),
    true,
    'returned highlighted search terms'
  )
})

t.test('npm help-search no args', async t => {
  await t.rejects(execHelpSearch(t), /npm help-search/, 'outputs usage')
})

t.test('npm help-search no matches', async t => {
  const { output } = await execHelpSearch(t, ['asdfasdf'])

  t.match(output, /No matches/)
})
