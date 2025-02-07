const functionA = () => {
  functionB()
}

function functionB() {
  functionC()
}

const functionC = () => {
  functionD()
}

const functionD = () => {
  (function functionE () {
    if (Math.random() > 0) {
      throw new Error('an error!')
    }
  })()
}

const thrower = functionA

try {
  thrower()
} catch (err) {
  throw err
}
