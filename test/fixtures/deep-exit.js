'use strict';

// This is meant to be run with --trace-exit.

const depth = parseInt(process.env.STACK_DEPTH) || 30;
let counter = 1;
function recurse() {
  if (counter++ < depth) {
    recurse();
  } else {
    process.exit(0);
  }
}

recurse();
