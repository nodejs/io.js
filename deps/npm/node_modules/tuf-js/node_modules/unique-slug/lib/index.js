'use strict'
var MurmurHash3 = require('imurmurhash')

module.exports = function (uniq) {
  if (uniq) {
    var hash = new MurmurHash3(uniq)
    return ('00000000' + hash.result().toString(16)).slice(-8)
  } else {
    return (Math.random().toString(16) + '0000000').slice(2, 10)
  }
}
