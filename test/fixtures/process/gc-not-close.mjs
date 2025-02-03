import { strictEqual } from 'assert'

function setup() {
  let obj = { foo: 'bar' }
  process.finalization.register(obj, shutdown)
  setImmediate(function () {
    obj = undefined
    gc()
  })
}

let shutdownCalled = false
function shutdown(obj) {
  shutdownCalled = true
}

setup()

process.on('exit', function () {
  strictEqual(shutdownCalled, false)
})
