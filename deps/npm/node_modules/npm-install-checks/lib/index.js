const semver = require('semver')

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

const isMusl = (file) => file.includes('libc.musl-') || file.includes('ld-musl-')

const checkPlatform = (target, force = false, environment = {}) => {
  if (force) {
    return
  }

  const platform = environment.os || process.platform
  const arch = environment.cpu || process.arch
  const osOk = target.os ? checkList(platform, target.os) : true
  const cpuOk = target.cpu ? checkList(arch, target.cpu) : true

  let libcOk = true
  let libcFamily = null
  if (target.libc) {
    // libc checks only work in linux, any value is a failure if we aren't
    if (environment.libc) {
      libcOk = checkList(environment.libc, target.libc)
    } else if (platform !== 'linux') {
      libcOk = false
    } else {
      const report = process.report.getReport()
      if (report.header?.glibcVersionRuntime) {
        libcFamily = 'glibc'
      } else if (Array.isArray(report.sharedObjects) && report.sharedObjects.some(isMusl)) {
        libcFamily = 'musl'
      }
      libcOk = libcFamily ? checkList(libcFamily, target.libc) : false
    }
  }

  if (!osOk || !cpuOk || !libcOk) {
    throw Object.assign(new Error('Unsupported platform'), {
      pkgid: target._id,
      current: {
        os: platform,
        cpu: arch,
        libc: libcFamily,
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
}
