class Foo {
  x;
  constructor (x = 33) {
    this.x = x ? x : 99
    if (this.x) {
      this.methodA()
    } else {
      this.methodB()
    }
    this.methodC()
  }
  methodA () {

  }
  methodB () {

  }
  methodC () {

  }
  methodD () {

  }
}

const a = new Foo(0)
const b = new Foo(33)
a.methodD()

declare const module: {
  exports: any
}

module.exports = {
  a,
  b,
  Foo,
}

// To recreate:
//
// npx tsc --outDir test/fixtures/source-map --inlineSourceMap --inlineSources test/fixtures/source-map/no-throw.ts
