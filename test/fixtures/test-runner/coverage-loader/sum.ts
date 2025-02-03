export function sum (...n) {
  if (!n.every((num) => typeof num === 'number')) throw new Error('Not a number')
  return n.reduce((acc, cur) => acc + cur)
}
