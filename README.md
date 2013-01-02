node-can
========

This is a NodeJS SocketCAN extension. SocketCAN is a socket-based implementation of the CANbus protocol for Linux system.

This extensions makes it possible to send and receive CAN messages (extended, remote transission) using simple Javascript functions.

Usage
-----

Basic CAN example:
```javascript
var can = require('can');

var channel = can.createRawChannel("vcan0", true);

// Log any message
channel.addListener("onMessage", function(msg) { console.log(msg); } );

// Reply any message
channel.addListener("onMessage", channel.send, channel);

channel.start();
```

Working with message and signals:
```javascript
var can = require('can');
var fs = require('fs');

// Parse database
var network = can.parseNetworkDescription("samples/can_definition_sample.kcd");
var channel = can.createRawChannel("vcan0");
var db      = new can.DatabaseService(channel, network.buses["Motor"].messages);

channel.start();

// Register a listener to get any value changes
db.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
   console.log("SpeedKm " + s.value);
}

// Update tank temperature
db.messages["TankController"].signals["TankTemperature"].update(80);

// Trigger sending this message
db.send("TankController");
```

Install
-------

There are two options for installing node-can:

1. Clone / download node-can from [GitHub](https://github.com/sebi2k1/node-can),
   then:

    node-waf clean && node-waf configure build

2. Install via npm:

    npm install can
