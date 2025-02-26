process.on('exit', (exitCode) => {
    console.log(`the exit listener received code: ${exitCode}`);
})

await new Promise(() => {});
