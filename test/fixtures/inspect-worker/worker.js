console.log("worker thread");
process.on('exit', () => {
  console.log('Worker1: Exiting...');
});
