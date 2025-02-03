import util = require("node:util");

const foo: string = "Hello, TypeScript!";

console.log(util.styleText(["red"], foo));
