import { isMainThread, Worker } from 'node:worker_threads';

if (isMainThread) {
  process.finalization.register({ foo: 'foo' }, () => {
    process.stdout.write('shutdown on main thread\n');
  });

  const worker = new Worker(import.meta.filename);

  worker.postMessage('ping');
} else {
  process.finalization.register({ foo: 'bar' }, () => {
    process.stdout.write('shutdown on worker\n');
  });
}
