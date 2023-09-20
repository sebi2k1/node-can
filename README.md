[![Build Status](https://github.com/sebi2k1/node-can/actions/workflows/cicd.yml/badge.svg?branch=master)](https://travis-ci.com/sebi2k1/node-can)

node-can
========

This is a NodeJS SocketCAN extension. SocketCAN is a socket-based implementation of the CANbus protocol for Linux system.

This extensions makes it possible to send and receive CAN messages (extended, remote transission) using simple Javascript functions.

Usage (JavaScript)
-----------------

Basic CAN example:
```javascript
var can = require("socketcan");

var channel = can.createRawChannel("vcan0", true);

// Log any message
channel.addListener("onMessage", function(msg) { console.log(msg); } );

// Reply any message
channel.addListener("onMessage", channel.send, channel);

channel.start();
```

Working with message and signals:
```javascript
var can = require("socketcan");

// Parse database
var network = can.parseNetworkDescription("samples/can_definition_sample.kcd");
var channel = can.createRawChannel("vcan0");
var db_motor = new can.DatabaseService(channel, network.buses["Motor"]);
var db_instr = new can.DatabaseService(channel, network.buses["Instrumentation"]);

channel.start();

// Register a listener to get any value changes
db_motor.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
   console.log("SpeedKm " + s.value);
});

// Register a listener to get any value updates
db_motor.messages["Emission"].signals["Enginespeed"].onUpdate(function(s) {
   console.log("Enginespeed " + s.value);
});

// Update tank temperature
db_instr.messages["TankController"].signals["TankTemperature"].update(80);

// Trigger sending this message
db_instr.send("TankController");

channel.stop()
```

Usage (TypeScript)
------------------

Basic CAN example:
```typescript
import * as can from "socketcan";

const channel = can.createRawChannel("vcan0", true);

// Log any message
channel.addListener("onMessage", function (msg: can.Message) { console.log(msg); });

// Reply any message
channel.addListener("onMessage", channel.send, channel);

channel.start();
```

Working with message and signals:

```typescript
import * as can from "socketcan"

// Parse database
const network = can.parseNetworkDescription("samples/can_definition_sample.kcd");
const channel = can.createRawChannel("vcan0");
const db_motor = new can.DatabaseService(channel, network.buses["Motor"]);
const db_instr = new can.DatabaseService(channel, network.buses["Instrumentation"]);

channel.start();

// Register a listener to get any value changes
db_motor.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function (s: can.Signal) {
	console.log("SpeedKm " + s.value);
});

// Register a listener to get any value updates
db_motor.messages["Emission"].signals["Enginespeed"].onUpdate(function (s: can.Signal) {
	console.log("Enginespeed " + s.value);
});

// Update tank temperature
db_instr.messages["TankController"].signals["TankTemperature"].update(80);

// Trigger sending this message
db_instr.send("TankController");

channel.stop();
```


Install
-------

There are two options for installing node-can:

1. Clone / download node-can from [GitHub](https://github.com/sebi2k1/node-can),
	 then:

```shell
    $ npm ci
    $ npm run configure
    $ npm run build:all
```

2. Install via npm:

```shell
    $ npm install socketcan
```
