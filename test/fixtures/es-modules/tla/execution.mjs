import process from 'node:process';
process._rawDebug('I am executed');
await Promise.resolve('hi');
