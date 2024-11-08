const semver = require('semver')
const currentEnv = require('./current-env')
const { checkDevEngines } = require('./dev-engines')

const checkEngine = (target, npmVer, nodeVer, force = false) => {
  const nodev = force ? null : nodeVer
  const eng = target.engines
  const opt = { includePrerelease: true }
  if (!eng) {
    return
  }

  const nodeFail = nodev && eng.node && !semver.satisfies(nodev, eng.node, opt)
  const npmFail = npmVer && eng.npm && !semver.satisfies(npmVer, eng.npm, opt)
  if (nodeFail || npmFail) {
    throw Object.assign(new Error('Unsupported engine'), {
      pkgid: target._id,
      current: { node: nodeVer, npm: npmVer },
      required: eng,
      code: 'EBADENGINE',
    })
  }
}

const checkPlatform = (target, force = false, environment = {}) => {
  if (force) {
    return
  }

  const os = environment.os || currentEnv.os()
  const cpu = environment.cpu || currentEnv.cpu()
  const libc = environment.libc || currentEnv.libc(os)

  const osOk = target.os ? checkList(os, target.os) : true
  const cpuOk = target.cpu ? checkList(cpu, target.cpu) : true
  let libcOk = target.libc ? checkList(libc, target.libc) : true
  if (target.libc && !libc) {
    libcOk = false
  }

  if (!osOk || !cpuOk || !libcOk) {
    throw Object.assign(new Error('Unsupported platform'), {
      pkgid: target._id,
      current: {
        os,
        cpu,
        libc,
      },
      required: {
        os: target.os,
        cpu: target.cpu,
        libc: target.libc,
      },
      code: 'EBADPLATFORM',
    })
  }
}

const checkList = (value, list) => {
  if (typeof list === 'string') {
    list = [list]
  }
  if (list.length === 1 && list[0] === 'any') {
    return true
  }
  // match none of the negated values, and at least one of the
  // non-negated values, if any are present.
  let negated = 0
  let match = false
  for (const entry of list) {
    const negate = entry.charAt(0) === '!'
    const test = negate ? entry.slice(1) : entry
    if (negate) {
      negated++
      if (value === test) {
        return false
      }
    } else {
      match = match || value === test
    }
  }
  return match || negated === list.length
}

module.exports = {
  checkEngine,
  checkPlatform,
  checkDevEngines,
  currentEnv,
}
