/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/run-script.js TAP list scripts no args > basic report 1`] = `
Lifecycle scripts included in x@1.2.3:
  test
    exit 2
  start
    node server.js
  stop
    node kill-server.js
available via \`npm run-script\`:
  preenv
    echo before the env
  postenv
    echo after the env
`

exports[`test/lib/commands/run-script.js TAP list scripts parseable > must match snapshot 1`] = `
test:exit 2
start:node server.js
stop:node kill-server.js
preenv:echo before the env
postenv:echo after the env
`

exports[`test/lib/commands/run-script.js TAP list scripts warn json > json report 1`] = `
{
  "test": "exit 2",
  "start": "node server.js",
  "stop": "node kill-server.js",
  "preenv": "echo before the env",
  "postenv": "echo after the env"
}
`

exports[`test/lib/commands/run-script.js TAP list scripts, only commands > must match snapshot 1`] = `
Lifecycle scripts included in x@1.2.3:
  preversion
    echo doing the version dance
`

exports[`test/lib/commands/run-script.js TAP list scripts, only non-commands > must match snapshot 1`] = `
Scripts available in x@1.2.3 via \`npm run-script\`:
  glorp
    echo doing the glerp glop
`

exports[`test/lib/commands/run-script.js TAP workspaces failed workspace run with succeeded runs > should log error msgs for each workspace script 1`] = `
Lifecycle script \`glorp\` failed with error:
code ERR
workspace a@1.0.0
location {CWD}/prefix/packages/a
ERR
`

exports[`test/lib/commands/run-script.js TAP workspaces list all scripts --json > must match snapshot 1`] = `
{
  "a": {
    "glorp": "echo a doing the glerp glop"
  },
  "b": {
    "glorp": "echo b doing the glerp glop"
  },
  "c": {
    "test": "exit 0",
    "posttest": "echo posttest",
    "lorem": "echo c lorem"
  },
  "d": {
    "test": "exit 0",
    "posttest": "echo posttest"
  },
  "e": {
    "test": "exit 0",
    "start": "echo start something"
  },
  "noscripts": {}
}
`

exports[`test/lib/commands/run-script.js TAP workspaces list all scripts --parseable > must match snapshot 1`] = `
a:glorp:echo a doing the glerp glop
b:glorp:echo b doing the glerp glop
c:test:exit 0
c:posttest:echo posttest
c:lorem:echo c lorem
d:test:exit 0
d:posttest:echo posttest
e:test:exit 0
e:start:echo start something
`

exports[`test/lib/commands/run-script.js TAP workspaces list all scripts > must match snapshot 1`] = `
Scripts available in a@1.0.0 via \`npm run-script\`:
  glorp
    echo a doing the glerp glop

Scripts available in b@2.0.0 via \`npm run-script\`:
  glorp
    echo b doing the glerp glop

Lifecycle scripts included in c@1.0.0:
  test
    exit 0
  posttest
    echo posttest
available via \`npm run-script\`:
  lorem
    echo c lorem

Lifecycle scripts included in d@1.0.0:
  test
    exit 0
  posttest
    echo posttest

Lifecycle scripts included in e:
  test
    exit 0
  start
    echo start something
`

exports[`test/lib/commands/run-script.js TAP workspaces list all scripts with colors > must match snapshot 1`] = `
[0m[1mScripts[22m[0m available in [36ma@1.0.0[39m via \`[34mnpm run-script[39m\`:
  glorp
    [2mecho a doing the glerp glop[22m

[0m[1mScripts[22m[0m available in [36mb@2.0.0[39m via \`[34mnpm run-script[39m\`:
  glorp
    [2mecho b doing the glerp glop[22m

[0m[1mLifecycle scripts[22m[0m included in [36mc@1.0.0[39m:
  test
    [2mexit 0[22m
  posttest
    [2mecho posttest[22m
available via \`[34mnpm run-script[39m\`:
  lorem
    [2mecho c lorem[22m

[0m[1mLifecycle scripts[22m[0m included in [36md@1.0.0[39m:
  test
    [2mexit 0[22m
  posttest
    [2mecho posttest[22m

[0m[1mLifecycle scripts[22m[0m included in [36me[39m:
  test
    [2mexit 0[22m
  start
    [2mecho start something[22m
`

exports[`test/lib/commands/run-script.js TAP workspaces list regular scripts, filtered by name > must match snapshot 1`] = `
Scripts available in a@1.0.0 via \`npm run-script\`:
  glorp
    echo a doing the glerp glop

Scripts available in b@2.0.0 via \`npm run-script\`:
  glorp
    echo b doing the glerp glop
`

exports[`test/lib/commands/run-script.js TAP workspaces list regular scripts, filtered by parent folder > must match snapshot 1`] = `
Scripts available in a@1.0.0 via \`npm run-script\`:
  glorp
    echo a doing the glerp glop

Scripts available in b@2.0.0 via \`npm run-script\`:
  glorp
    echo b doing the glerp glop

Lifecycle scripts included in c@1.0.0:
  test
    exit 0
  posttest
    echo posttest
available via \`npm run-script\`:
  lorem
    echo c lorem

Lifecycle scripts included in d@1.0.0:
  test
    exit 0
  posttest
    echo posttest

Lifecycle scripts included in e:
  test
    exit 0
  start
    echo start something
`

exports[`test/lib/commands/run-script.js TAP workspaces list regular scripts, filtered by path > must match snapshot 1`] = `
Scripts available in a@1.0.0 via \`npm run-script\`:
  glorp
    echo a doing the glerp glop
`

exports[`test/lib/commands/run-script.js TAP workspaces missing scripts in all workspaces > should log error msgs for each workspace script 1`] = `
Lifecycle script \`missing-script\` failed with error:
workspace a@1.0.0
location {CWD}/prefix/packages/a
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=a@1.0.0
Lifecycle script \`missing-script\` failed with error:
workspace b@2.0.0
location {CWD}/prefix/packages/b
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=b@2.0.0
Lifecycle script \`missing-script\` failed with error:
workspace c@1.0.0
location {CWD}/prefix/packages/c
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=c@1.0.0
Lifecycle script \`missing-script\` failed with error:
workspace d@1.0.0
location {CWD}/prefix/packages/d
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=d@1.0.0
Lifecycle script \`missing-script\` failed with error:
workspace e
location {CWD}/prefix/packages/e
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=e
Lifecycle script \`missing-script\` failed with error:
workspace noscripts@1.0.0
location {CWD}/prefix/packages/noscripts
Missing script: "missing-script"
npm error
To see a list of scripts, run:
  npm run --workspace=noscripts@1.0.0
`

exports[`test/lib/commands/run-script.js TAP workspaces missing scripts in some workspaces > should log error msgs for each workspace script 1`] = `
Lifecycle script \`test\` failed with error:
workspace a@1.0.0
location {CWD}/prefix/packages/a
Missing script: "test"
npm error
To see a list of scripts, run:
  npm run --workspace=a@1.0.0
Lifecycle script \`test\` failed with error:
workspace b@2.0.0
location {CWD}/prefix/packages/b
Missing script: "test"
npm error
To see a list of scripts, run:
  npm run --workspace=b@2.0.0
`

exports[`test/lib/commands/run-script.js TAP workspaces single failed workspace run > should log error msgs for each workspace script 1`] = `
Lifecycle script \`test\` failed with error:
workspace c@1.0.0
location {CWD}/prefix/packages/c
err
`
