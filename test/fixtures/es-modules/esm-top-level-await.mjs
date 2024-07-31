import { setTimeout } from 'node:timers/promises';

// Waiting some arbitrary amount of time to make sure other tasks won't start
// executing in the mean time.
await setTimeout(9);
console.log(1);
console.log(2);
