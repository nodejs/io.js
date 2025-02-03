export const foo = 'bar';

function myFunc() {
  throw { message: 'foobar' };
}
myFunc();
