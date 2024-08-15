const util = require('node:util');

const text: string = 'Hello, TypeScript!';

console.log(util.styleText(['bold', 'red'], text));

module.exports = {
  text
};
