node-can
========

This is a NodeJS SocketCAN extension. SocketCAN is a socket-based implementation of the CANbus protocol for Linux system.

This extensions makes it possible to send and receive CAN messages (extended, remote transission) using simple Javascript functions.

Usage
-----

```javascript
var can = require('can');

var channel = can.createRawChannel("vcan0", true);

// Log any message
channel.addListener("onMessage", function(msg) { console.log(msg); } );

// Reply any message
channel.addListener("onMessage", channel.send, channel);

channel.start();
```

Installation
------------

There are two options for installing node-can:

1. Clone / download node-can from [github](https://github.com/sebi2k1/node-can),
   then:

    node-waf clean && node-waf configure build

2. Install via npm:

    npm install can
