const satisfies = require('semver/functions/satisfies')
const validRange = require('semver/ranges/valid')

const recognizedOnFail = [
  'ignore',
  'warn',
  'error',
  'download',
]

const recognizedProperties = [
  'name',
  'version',
  'onFail',
]

const recognizedEngines = [
  'packageManager',
  'runtime',
  'cpu',
  'libc',
  'os',
]

/** checks a devEngine dependency */
function checkDependency (wanted, current, opts) {
  const { engine } = opts

  if ((typeof wanted !== 'object' || wanted === null) || Array.isArray(wanted)) {
    throw new Error(`Invalid non-object value for "${engine}"`)
  }

  const properties = Object.keys(wanted)

  for (const prop of properties) {
    if (!recognizedProperties.includes(prop)) {
      throw new Error(`Invalid property "${prop}" for "${engine}"`)
    }
  }

  if (!properties.includes('name')) {
    throw new Error(`Missing "name" property for "${engine}"`)
  }

  if (typeof wanted.name !== 'string') {
    throw new Error(`Invalid non-string value for "name" within "${engine}"`)
  }

  if (typeof current.name !== 'string' || current.name === '') {
    throw new Error(`Unable to determine "name" for "${engine}"`)
  }

  if (properties.includes('onFail')) {
    if (typeof wanted.onFail !== 'string') {
      throw new Error(`Invalid non-string value for "onFail" within "${engine}"`)
    }
    if (!recognizedOnFail.includes(wanted.onFail)) {
      throw new Error(`Invalid onFail value "${wanted.onFail}" for "${engine}"`)
    }
  }

  if (wanted.name !== current.name) {
    return new Error(
      `Invalid name "${wanted.name}" does not match "${current.name}" for "${engine}"`
    )
  }

  if (properties.includes('version')) {
    if (typeof wanted.version !== 'string') {
      throw new Error(`Invalid non-string value for "version" within "${engine}"`)
    }
    if (typeof current.version !== 'string' || current.version === '') {
      throw new Error(`Unable to determine "version" for "${engine}" "${wanted.name}"`)
    }
    if (validRange(wanted.version)) {
      if (!satisfies(current.version, wanted.version, opts.semver)) {
        return new Error(
          // eslint-disable-next-line max-len
          `Invalid semver version "${wanted.version}" does not match "${current.version}" for "${engine}"`
        )
      }
    } else if (wanted.version !== current.version) {
      return new Error(
        `Invalid version "${wanted.version}" does not match "${current.version}" for "${engine}"`
      )
    }
  }
}

/** checks devEngines package property and returns array of warnings / errors */
function checkDevEngines (wanted, current = {}, opts = {}) {
  if ((typeof wanted !== 'object' || wanted === null) || Array.isArray(wanted)) {
    throw new Error(`Invalid non-object value for devEngines`)
  }

  const errors = []

  for (const engine of Object.keys(wanted)) {
    if (!recognizedEngines.includes(engine)) {
      throw new Error(`Invalid property "${engine}"`)
    }
    const dependencyAsAuthored = wanted[engine]
    const dependencies = [dependencyAsAuthored].flat()
    const currentEngine = current[engine] || {}

    // this accounts for empty array eg { runtime: [] } and ignores it
    if (dependencies.length === 0) {
      continue
    }

    const depErrors = []
    for (const dep of dependencies) {
      const result = checkDependency(dep, currentEngine, { ...opts, engine })
      if (result) {
        depErrors.push(result)
      }
    }

    const invalid = depErrors.length === dependencies.length

    if (invalid) {
      const lastDependency = dependencies[dependencies.length - 1]
      let onFail = lastDependency.onFail || 'error'
      if (onFail === 'download') {
        onFail = 'error'
      }

      const err = Object.assign(new Error(`Invalid engine "${engine}"`), {
        errors: depErrors,
        engine,
        isWarn: onFail === 'warn',
        isError: onFail === 'error',
        current: currentEngine,
        required: dependencyAsAuthored,
      })

      errors.push(err)
    }
  }
  return errors
}

module.exports = {
  checkDevEngines,
}
