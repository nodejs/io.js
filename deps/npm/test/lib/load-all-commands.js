// Our coverage mapping means that stuff like this doen't count for coverage.
// It does ensure that every command has a usage that renders, contains its
// name, a description, and if it has completion it is a function.  That it
// renders also ensures that any params we've defined in our commands work.
const t = require('tap')
const util = require('node:util')
const { load: loadMockNpm } = require('../fixtures/mock-npm.js')
const { commands } = require('../../lib/utils/cmd-list.js')
const BaseCommand = require('../../lib/base-cmd.js')

const isAsyncFn = (v) => typeof v === 'function' && /^\[AsyncFunction:/.test(util.inspect(v))

t.test('load each command', async t => {
  const counts = {
    completion: 0,
    ignoreImplicitWorkspace: 0,
    workspaces: 0,
    noParams: 0,
  }

  for (const cmd of commands) {
    await t.test(cmd, async t => {
      const { npm, outputs, cmd: impl } = await loadMockNpm(t, {
        command: cmd,
        config: { usage: true },
      })
      const ctor = impl.constructor

      t.notOk(impl.completion, 'completion is static, not on instance')
      if (ctor.completion) {
        t.ok(isAsyncFn(ctor.completion), 'completion is async function')
        counts.completion++
      }

      // exec fn
      t.ok(isAsyncFn(impl.exec), 'exec is async')
      t.ok(impl.exec.length <= 1, 'exec fn has 0 or 1 args')

      // workspaces
      t.type(ctor.ignoreImplicitWorkspace, 'boolean', 'ctor has ignoreImplictWorkspace boolean')
      if (ctor.ignoreImplicitWorkspace !== BaseCommand.ignoreImplicitWorkspace) {
        counts.ignoreImplicitWorkspace++
      }

      t.type(ctor.workspaces, 'boolean', 'ctor has workspaces boolean')
      if (ctor.workspaces !== BaseCommand.workspaces) {
        counts.workspaces++
      }

      if (ctor.workspaces) {
        t.ok(isAsyncFn(impl.execWorkspaces), 'execWorkspaces is async')
        t.ok(impl.exec.length <= 1, 'execWorkspaces fn has 0 or 1 args')
      } else {
        t.notOk(impl.execWorkspaces, 'has no execWorkspaces fn')
      }

      // name/desc
      t.ok(impl.description, 'implementation has a description')
      t.equal(impl.description, ctor.description, 'description is same on instance and ctor')
      t.ok(impl.name, 'implementation has a name')
      t.equal(impl.name, ctor.name, 'name is same on instance and ctor')
      t.equal(cmd, impl.name, 'command list and name are the same')

      // params are optional
      if (impl.params) {
        t.equal(impl.params, ctor.params, 'params is same on instance and ctor')
        t.ok(impl.params, 'implementation has a params')
      } else {
        counts.noParams++
      }

      // usage
      t.match(impl.usage, cmd, 'usage contains the command')
      await npm.exec(cmd, [])
      t.match(outputs[0], impl.usage, 'usage is what is output')
      t.match(outputs[0], ctor.describeUsage, 'usage is what is output')
      t.notOk(impl.describeUsage, 'describe usage is only static')
    })
  }

  // make sure refactors dont move or rename these static properties since
  // we guard against the tests for them above
  t.ok(counts.completion > 0, 'has some completion functions')
  t.ok(counts.ignoreImplicitWorkspace > 0, 'has some commands that change ignoreImplicitWorkspace')
  t.ok(counts.workspaces > 0, 'has some commands that change workspaces')
  t.ok(counts.noParams > 0, 'has some commands that do not have params')
})
