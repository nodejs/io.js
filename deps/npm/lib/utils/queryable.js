const util = require('node:util')
const _delete = Symbol('delete')
const _append = Symbol('append')

const sqBracketsMatcher = str => str.match(/(.+)\[([^\]]+)\]\.?(.*)$/)

// replaces any occurrence of an empty-brackets (e.g: []) with a special
// Symbol(append) to represent it, this is going to be useful for the setter
// method that will push values to the end of the array when finding these
const replaceAppendSymbols = str => {
  const matchEmptyBracket = str.match(/^(.*)\[\]\.?(.*)$/)

  if (matchEmptyBracket) {
    const [, pre, post] = matchEmptyBracket
    return [...replaceAppendSymbols(pre), _append, post].filter(Boolean)
  }

  return [str]
}

const parseKeys = key => {
  const sqBracketItems = new Set()
  sqBracketItems.add(_append)
  const parseSqBrackets = str => {
    const index = sqBracketsMatcher(str)

    // once we find square brackets, we recursively parse all these
    if (index) {
      const preSqBracketPortion = index[1]

      // we want to have a `new String` wrapper here in order to differentiate
      // between multiple occurrences of the same string, e.g:
      // foo.bar[foo.bar] should split into { foo: { bar: { 'foo.bar': {} } }
      /* eslint-disable-next-line no-new-wrappers */
      const foundKey = new String(index[2])
      const postSqBracketPortion = index[3]

      // we keep track of items found during this step to make sure
      // we don't try to split-separate keys that were defined within
      // square brackets, since the key name itself might contain dots
      sqBracketItems.add(foundKey)

      // returns an array that contains either dot-separate items (that will
      // be split apart during the next step OR the fully parsed keys
      // read from square brackets, e.g:
      // foo.bar[1.0.0].a.b -> ['foo.bar', '1.0.0', 'a.b']
      return [
        ...parseSqBrackets(preSqBracketPortion),
        foundKey,
        ...(postSqBracketPortion ? parseSqBrackets(postSqBracketPortion) : []),
      ]
    }

    // at the end of parsing, any usage of the special empty-bracket syntax
    // (e.g: foo.array[]) has  not yet been parsed, here we'll take care
    // of parsing it and adding a special symbol to represent it in
    // the resulting list of keys
    return replaceAppendSymbols(str)
  }

  const res = []
  // starts by parsing items defined as square brackets, those might be
  // representing properties that have a dot in the name or just array
  // indexes, e.g: foo[1.0.0] or list[0]
  const sqBracketKeys = parseSqBrackets(key.trim())

  for (const k of sqBracketKeys) {
    // keys parsed from square brackets should just be added to list of
    // resulting keys as they might have dots as part of the key
    if (sqBracketItems.has(k)) {
      res.push(k)
    } else {
      // splits the dot-sep property names and add them to the list of keys
      /* eslint-disable-next-line no-new-wrappers */
      for (const splitKey of k.split('.')) {
        res.push(String(splitKey))
      }
    }
  }

  // returns an ordered list of strings in which each entry
  // represents a key in an object defined by the previous entry
  return res
}

const getter = ({ data, key }, { unwrapSingleItemArrays = true } = {}) => {
  // keys are a list in which each entry represents the name of
  // a property that should be walked through the object in order to
  // return the final found value
  const keys = parseKeys(key)
  let _data = data
  let label = ''

  for (const k of keys) {
    // empty-bracket-shortcut-syntax is not supported on getter
    if (k === _append) {
      throw Object.assign(new Error('Empty brackets are not valid syntax for retrieving values.'), {
        code: 'EINVALIDSYNTAX',
      })
    }

    // extra logic to take into account printing array, along with its
    // special syntax in which using a dot-sep property name after an
    // arry will expand it's results, e.g:
    // arr.name -> arr[0].name=value, arr[1].name=value, ...
    const maybeIndex = Number(k)
    if (Array.isArray(_data) && !Number.isInteger(maybeIndex)) {
      _data = _data.reduce((acc, i, index) => {
        acc[`${label}[${index}].${k}`] = i[k]
        return acc
      }, {})
      return _data
    } else {
      if (!Object.hasOwn(_data, k)) {
        return undefined
      }
      _data = _data[k]
    }

    label += k
  }

  // these are some legacy expectations from
  // the old API consumed by lib/view.js
  if (unwrapSingleItemArrays && Array.isArray(_data) && _data.length <= 1) {
    _data = _data[0]
  }

  return {
    [key]: _data,
  }
}

const setter = ({ data, key, value, force }) => {
  // setter goes to recursively transform the provided data obj,
  // setting properties from the list of parsed keys, e.g:
  // ['foo', 'bar', 'baz'] -> { foo: { bar: { baz:  {} } }
  const keys = parseKeys(key)
  const setKeys = (_data, _key) => {
    // handles array indexes, converting valid integers to numbers,
    // note that occurrences of Symbol(append) will throw,
    // so we just ignore these for now
    let maybeIndex = Number.NaN
    try {
      maybeIndex = Number(_key)
    } catch {
      // leave it NaN
    }
    if (!Number.isNaN(maybeIndex)) {
      _key = maybeIndex
    }

    // creates new array in case key is an index
    // and the array obj is not yet defined
    const keyIsAnArrayIndex = _key === maybeIndex || _key === _append
    const dataHasNoItems = !Object.keys(_data).length
    if (keyIsAnArrayIndex && dataHasNoItems && !Array.isArray(_data)) {
      _data = []
    }

    // converting from array to an object is also possible, in case the
    // user is using force mode, we should also convert existing arrays
    // to an empty object if the current _data is an array
    if (force && Array.isArray(_data) && !keyIsAnArrayIndex) {
      _data = { ..._data }
    }

    // the _append key is a special key that is used to represent
    // the empty-bracket notation, e.g: arr[] -> arr[arr.length]
    if (_key === _append) {
      if (!Array.isArray(_data)) {
        throw Object.assign(new Error(`Can't use append syntax in non-Array element`), {
          code: 'ENOAPPEND',
        })
      }
      _key = _data.length
    }

    // retrieves the next data object to recursively iterate on,
    // throws if trying to override a literal value or add props to an array
    const next = () => {
      const haveContents = !force && _data[_key] != null && value !== _delete
      const shouldNotOverrideLiteralValue = !(typeof _data[_key] === 'object')
      // if the next obj to recurse is an array and the next key to be
      // appended to the resulting obj is not an array index, then it
      // should throw since we can't append arbitrary props to arrays
      const shouldNotAddPropsToArrays =
        typeof keys[0] !== 'symbol' && Array.isArray(_data[_key]) && Number.isNaN(Number(keys[0]))

      const overrideError = haveContents && shouldNotOverrideLiteralValue
      if (overrideError) {
        throw Object.assign(
          new Error(`Property ${_key} already exists and is not an Array or Object.`),
          { code: 'EOVERRIDEVALUE' }
        )
      }

      const addPropsToArrayError = haveContents && shouldNotAddPropsToArrays
      if (addPropsToArrayError) {
        throw Object.assign(new Error(`Can't add property ${key} to an Array.`), {
          code: 'ENOADDPROP',
        })
      }

      return typeof _data[_key] === 'object' ? _data[_key] || {} : {}
    }

    // sets items from the parsed array of keys as objects, recurses to
    // setKeys in case there are still items to be handled, otherwise it
    // just sets the original value set by the user
    if (keys.length) {
      _data[_key] = setKeys(next(), keys.shift())
    } else {
      // handles special deletion cases for obj props / array items
      if (value === _delete) {
        if (Array.isArray(_data)) {
          _data.splice(_key, 1)
        } else {
          delete _data[_key]
        }
      } else {
        // finally, sets the value in its right place
        _data[_key] = value
      }
    }

    return _data
  }

  setKeys(data, keys.shift())
}

class Queryable {
  static ALL = ''

  #data = null

  constructor (obj) {
    if (!obj || typeof obj !== 'object') {
      throw Object.assign(new Error('Queryable needs an object to query properties from.'), {
        code: 'ENOQUERYABLEOBJ',
      })
    }

    this.#data = obj
  }

  query (queries, opts) {
    // this ugly interface here is meant to be a compatibility layer
    // with the legacy API lib/view.js is consuming, if at some point
    // we refactor that command then we can revisit making this nicer
    if (queries === Queryable.ALL) {
      return { [Queryable.ALL]: this.#data }
    }

    const q = query =>
      getter({
        data: this.#data,
        key: query,
      }, opts)

    if (Array.isArray(queries)) {
      let res = {}
      for (const query of queries) {
        res = { ...res, ...q(query) }
      }
      return res
    } else {
      return q(queries)
    }
  }

  // return the value for a single query if found, otherwise returns undefined
  get (query) {
    const obj = this.query(query)
    if (obj) {
      return obj[query]
    }
  }

  // creates objects along the way for the provided `query` parameter
  // and assigns `value` to the last property of the query chain
  set (query, value, { force } = {}) {
    setter({
      data: this.#data,
      key: query,
      value,
      force,
    })
  }

  // deletes the value of the property found at `query`
  delete (query) {
    setter({
      data: this.#data,
      key: query,
      value: _delete,
    })
  }

  toJSON () {
    return this.#data
  }

  [util.inspect.custom] () {
    return this.toJSON()
  }
}

module.exports = Queryable
