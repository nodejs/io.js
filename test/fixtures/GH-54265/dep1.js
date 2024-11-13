// dep1.js
module.exports = function requireDep2() {
  require("./dep2.js");
};
