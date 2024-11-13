'use strict';

const common = require('../common');
const net = require('net');
// Process should exit because it does not create a real TCP server.
// Pass localhost to ensure create TCP handle asynchronously because it causes DNS resolution.
net.createServer().listen(0, 'localhost', common.mustNotCall()).close();
