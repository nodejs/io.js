const address = process.env.SERVER_ADDRESS;
const response = await fetch(address);
const body = await response.text();
console.log(body);
