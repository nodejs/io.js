const net = require('net');

exports.onConnect = function (req, clientSocket, head) {
  const [hostname, port] = req.url.split(':');

  const serverSocket = net.connect(port, hostname, () => {
    clientSocket.write(
      'HTTP/1.1 200 Connection Established\r\n' +
      'Proxy-agent: Node.js-Proxy\r\n' +
      '\r\n'
    );
    serverSocket.write(head);
    clientSocket.pipe(serverSocket);
    serverSocket.pipe(clientSocket);
  });

  serverSocket.on('error', (err) => {
    console.error('Error on CONNECT tunnel:', err.message);
    clientSocket.write('HTTP/1.1 500 Connection Error\r\n\r\n');
    clientSocket.end();
  });
};
