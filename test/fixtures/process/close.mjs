import { strictEqual } from 'assert'

function setup() {
  const obj = { foo: 'bar' }
  process.finalization.register(obj, shutdown)
}

let shutdownCalled = false
function shutdown(obj) {
  shutdownCalled = true
  strictEqual(obj.foo, 'bar')
}

setup()

process.on('exit', function () {
  strictEqual(shutdownCalled, true)
})
