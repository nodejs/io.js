const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm')
const reifyOutput = require('../../../lib/utils/reify-output.js')

t.cleanSnapshot = str => str.replace(/in [0-9]+m?s/g, 'in {TIME}')

const mockReify = async (t, reify, { command, ...config } = {}) => {
  const mock = await mockNpm(t, {
    command,
    config,
  })

  // Hack to adapt existing fake test. Make npm.command
  // return whatever was passed in to this function.
  // What it should be doing is npm.exec(command) but that
  // breaks most of these tests because they dont expect
  // a command to actually run.
  Object.defineProperty(mock.npm, 'command', {
    get () {
      return command
    },
    enumerable: true,
  })

  reifyOutput(mock.npm, reify)
  mock.npm.finish()

  return mock.joinedOutput()
}

t.test('missing info', async t => {
  const out = await mockReify(t, {
    actualTree: {
      children: [],
    },
    diff: {
      children: [],
    },
  })

  t.notMatch(
    out,
    'looking for funding',
    'should not print fund message if missing info'
  )
})

t.test('even more missing info', async t => {
  const out = await mockReify(t, {
    actualTree: {
      children: [],
    },
  })

  t.notMatch(
    out,
    'looking for funding',
    'should not print fund message if missing info'
  )
})

t.test('single package', async t => {
  const out = await mockReify(t, {
    // a report with an error is the same as no report at all, if
    // the command is not 'audit'
    auditReport: {
      error: {
        message: 'no audit for youuuuu',
      },
    },
    actualTree: {
      name: 'foo',
      package: {
        name: 'foo',
        version: '1.0.0',
      },
      edgesOut: new Map([
        ['bar', {
          to: {
            name: 'bar',
            package: {
              name: 'bar',
              version: '1.0.0',
              funding: { type: 'foo', url: 'http://example.com' },
            },
          },
        }],
      ]),
    },
    diff: {
      children: [],
    },
  })

  t.match(
    out,
    '1 package is looking for funding',
    'should print single package message'
  )
})

t.test('no message when funding config is false', async t => {
  const out = await mockReify(t, {
    actualTree: {
      name: 'foo',
      package: {
        name: 'foo',
        version: '1.0.0',
      },
      edgesOut: new Map([
        ['bar', {
          to: {
            name: 'bar',
            package: {
              name: 'bar',
              version: '1.0.0',
              funding: { type: 'foo', url: 'http://example.com' },
            },
          },
        }],
      ]),
    },
    diff: {
      children: [],
    },
  }, { fund: false })

  t.notMatch(out, 'looking for funding', 'should not print funding info')
})

t.test('print appropriate message for many packages', async t => {
  const out = await mockReify(t, {
    actualTree: {
      name: 'foo',
      package: {
        name: 'foo',
        version: '1.0.0',
      },
      edgesOut: new Map([
        ['bar', {
          to: {
            name: 'bar',
            package: {
              name: 'bar',
              version: '1.0.0',
              funding: { type: 'foo', url: 'http://example.com' },
            },
          },
        }],
        ['lorem', {
          to: {
            name: 'lorem',
            package: {
              name: 'lorem',
              version: '1.0.0',
              funding: { type: 'foo', url: 'http://example.com' },
            },
          },
        }],
        ['ipsum', {
          to: {
            name: 'ipsum',
            package: {
              name: 'ipsum',
              version: '1.0.0',
              funding: { type: 'foo', url: 'http://example.com' },
            },
          },
        }],
      ]),
    },
    diff: {
      children: [],
    },
  })

  t.match(
    out,
    '3 packages are looking for funding',
    'should print single package message'
  )
})

t.test('showing and not showing audit report', async t => {
  const auditReport = {
    toJSON: () => auditReport,
    auditReportVersion: 2,
    vulnerabilities: {
      minimist: {
        name: 'minimist',
        severity: 'low',
        via: [
          {
            id: 1179,
            url: 'https://npmjs.com/advisories/1179',
            title: 'Prototype Pollution',
            severity: 'low',
            vulnerable_versions: '<0.2.1 || >=1.0.0 <1.2.3',
          },
        ],
        effects: [],
        range: '<0.2.1 || >=1.0.0 <1.2.3',
        nodes: [
          'node_modules/minimist',
        ],
        fixAvailable: true,
      },
    },
    metadata: {
      vulnerabilities: {
        info: 0,
        low: 1,
        moderate: 0,
        high: 0,
        critical: 0,
        total: 1,
      },
      dependencies: {
        prod: 1,
        dev: 0,
        optional: 0,
        peer: 0,
        peerOptional: 0,
        total: 1,
      },
    },
  }

  t.test('no output when silent', async t => {
    const out = await mockReify(t, {
      actualTree: { inventory: { size: 999 }, children: [] },
      auditReport,
      diff: {
        children: [
          { action: 'ADD', ideal: { location: 'loc' } },
        ],
      },
    }, { silent: true })
    t.equal(out, '', 'should not get output when silent')
  })

  t.test('output when not silent', async t => {
    const out = await mockReify(t, {
      actualTree: { inventory: new Map(), children: [] },
      auditReport,
      diff: {
        children: [
          { action: 'ADD', ideal: { location: 'loc' } },
        ],
      },
    })

    t.match(out, /Run `npm audit` for details\.$/, 'got audit report')
  })

  for (const json of [true, false]) {
    t.test(`json=${json}`, async t => {
      t.test('set exit code when cmd is audit', async t => {
        await mockReify(t, {
          actualTree: { inventory: new Map(), children: [] },
          auditReport,
          diff: {
            children: [
              { action: 'ADD', ideal: { location: 'loc' } },
            ],
          },
        }, { command: 'audit', 'audit-level': 'low' })

        t.equal(process.exitCode, 1, 'set exit code')
      })

      t.test('do not set exit code when cmd is install', async t => {
        await mockReify(t, {
          actualTree: { inventory: new Map(), children: [] },
          auditReport,
          diff: {
            children: [
              { action: 'ADD', ideal: { location: 'loc' } },
            ],
          },
        }, { command: 'install', 'audit-level': 'low' })

        t.notOk(process.exitCode, 'did not set exit code')
      })
    })
  }
})

t.test('packages changed message', async t => {
  // return a test function that builds up the mock and snapshots output
  const testCase = async (t, added, removed, changed, audited, json, command) => {
    const mock = {
      actualTree: {
        inventory: { size: audited, has: () => true },
        children: [],
      },
      auditReport: audited ? {
        toJSON: () => mock.auditReport,
        vulnerabilities: {},
        metadata: {
          vulnerabilities: {
            total: 0,
          },
        },
      } : null,
      diff: {
        children: [
          { action: 'some random unexpected junk' },
        ],
      },
    }
    for (let i = 0; i < added; i++) {
      mock.diff.children.push({ action: 'ADD', ideal: { location: 'loc' } })
    }

    for (let i = 0; i < removed; i++) {
      mock.diff.children.push({ action: 'REMOVE', actual: { location: 'loc' } })
    }

    for (let i = 0; i < changed; i++) {
      const actual = { location: 'loc' }
      const ideal = { location: 'loc' }
      mock.diff.children.push({ action: 'CHANGE', actual, ideal })
    }

    const out = await mockReify(t, mock, { json, command })
    t.matchSnapshot(out, JSON.stringify({
      added,
      removed,
      changed,
      audited,
      json,
    }))
  }

  const cases = []
  for (const added of [0, 1, 2]) {
    for (const removed of [0, 1, 2]) {
      for (const changed of [0, 1, 2]) {
        for (const audited of [0, 1, 2]) {
          for (const json of [true, false]) {
            cases.push([added, removed, changed, audited, json, 'install'])
          }
        }
      }
    }
  }

  // add case for when audit is the command
  cases.push([0, 0, 0, 2, true, 'audit'])
  cases.push([0, 0, 0, 2, false, 'audit'])

  for (const c of cases) {
    await t.test('', t => testCase(t, ...c))
  }
})

t.test('added packages should be looked up within returned tree', async t => {
  t.test('has added pkg in inventory', async t => {
    const out = await mockReify(t, {
      actualTree: {
        name: 'foo',
        inventory: {
          has: () => true,
        },
      },
      diff: {
        children: [
          { action: 'ADD', ideal: { name: 'baz' } },
        ],
      },
    })

    t.matchSnapshot(out)
  })

  t.test('missing added pkg in inventory', async t => {
    const out = await mockReify(t, {
      actualTree: {
        name: 'foo',
        inventory: {
          has: () => false,
        },
      },
      diff: {
        children: [
          { action: 'ADD', ideal: { name: 'baz' } },
        ],
      },
    })

    t.matchSnapshot(out)
  })
})

t.test('prints dedupe difference on dry-run', async t => {
  const mock = {
    actualTree: {
      name: 'foo',
      inventory: {
        has: () => false,
      },
    },
    diff: {
      children: [
        { action: 'ADD', ideal: { name: 'foo', package: { version: '1.0.0' } } },
        { action: 'REMOVE', actual: { name: 'bar', package: { version: '1.0.0' } } },
        {
          action: 'CHANGE',
          actual: { name: 'bar', package: { version: '1.0.0' } },
          ideal: { package: { version: '2.1.0' } },
        },
      ],
    },
  }

  const out = await mockReify(t, mock, {
    'dry-run': true,
  })

  t.matchSnapshot(out, 'diff table')
})

t.test('prints dedupe difference on long', async t => {
  const mock = {
    actualTree: {
      name: 'foo',
      inventory: {
        has: () => false,
      },
    },
    diff: {
      children: [
        { action: 'ADD', ideal: { name: 'foo', package: { version: '1.0.0' } } },
        { action: 'REMOVE', actual: { name: 'bar', package: { version: '1.0.0' } } },
        {
          action: 'CHANGE',
          actual: { name: 'bar', package: { version: '1.0.0' } },
          ideal: { package: { version: '2.1.0' } },
        },
      ],
    },
  }

  const out = await mockReify(t, mock, {
    long: true,
  })

  t.matchSnapshot(out, 'diff table')
})
