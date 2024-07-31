function foo(str: string) {
  return str;
}

foo('noop');

// To recreate (Windows only):
//
// const filePath = require.resolve('./test/fixtures/source-map/ts-node.ts');
// const compiled = require('ts-node').create({ transpileOnly: true }).compile(fs.readFileSync(filePath, 'utf8'), filePath);
// fs.writeFileSync('test/fixtures/source-map/ts-node-win32.js', compiled);
