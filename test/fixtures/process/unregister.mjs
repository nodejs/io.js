import { strictEqual } from 'assert'

function setup() {
  const obj = { foo: 'bar' }
  process.finalization.register(obj, shutdown)
  setImmediate(function () {
    process.finalization.unregister(obj)
    process.finalization.unregister(obj) // twice, this should not throw
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
