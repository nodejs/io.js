const packageEnvs = (vals, prefix, env = {}) => {
  for (const [key, val] of Object.entries(vals)) {
    if (val === undefined) {
      continue
    } else if (val === null || val === false) {
      env[`${prefix}${key}`] = ''
    } else if (Array.isArray(val)) {
      val.forEach((item, index) => {
        packageEnvs({ [`${key}_${index}`]: item }, `${prefix}`, env)
      })
    } else if (typeof val === 'object') {
      packageEnvs(val, `${prefix}${key}_`, env)
    } else {
      env[`${prefix}${key}`] = String(val)
    }
  }
  return env
}

// https://github.com/npm/rfcs/pull/183 defines which fields we put into the environment
module.exports = pkg => {
  return packageEnvs({
    name: pkg.name,
    version: pkg.version,
    config: pkg.config,
    engines: pkg.engines,
    bin: pkg.bin,
  }, 'npm_package_')
}
