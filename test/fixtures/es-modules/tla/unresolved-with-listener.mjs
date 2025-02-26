process.on('exit', (exitCode) => {
    console.log(`the exit listener received code: ${exitCode}`);
    console.log(`process.exitCode inside the exist listener: ${process.exitCode}`);
})

await new Promise(() => {});
