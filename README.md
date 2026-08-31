![Build Status](https://github.com/sebi2k1/node-can/actions/workflows/cicd.yml/badge.svg?branch=master)

node-can
========

This is a NodeJS SocketCAN extension. SocketCAN is a socket-based implementation of the CANbus protocol for Linux system.

This extensions makes it possible to send and receive CAN messages (extended, remote transission) using simple Javascript/Typescript functions.

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

Changing the bitrate of a physical CAN interface:

```javascript
var can = require("socketcan");

can.setCanBitrate("can0", 500000);
```

`setCanBitrate` accepts integer arbitration bitrates from 1 kbit/s through
1 Mbit/s. If the interface is active, it is stopped while the bitrate is
changed and then restored to the active state; an already stopped interface
remains stopped. The process needs permission to administer network
interfaces (for example, `CAP_NET_ADMIN`).

Usage (TypeScript)
------------------

Basic CAN example:
```typescript
import {Message} from "*can.node";
import * as can from "socketcan";

const channel = can.createRawChannel("vcan0", true);

// Log any message
channel.addListener("onMessage", function (msg: Message) { console.log(msg); });

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

Documentation
-------

[API documentation](https://sebi2k1.github.io/node-can) hosted on GitHub pages.

Install
-------

The native addon links against
[libsocketcan](https://git.pengutronix.de/cgit/tools/libsocketcan/). Install its
development package before installing `socketcan`. On Debian and Ubuntu:

```shell
    $ sudo apt-get install libsocketcan-dev
```

Use the equivalent `libsocketcan` development package on other Linux
distributions.

There are two options for installing node-can:

1. Clone / download node-can from [GitHub](https://github.com/sebi2k1/node-can), then:

```shell
    $ pnpm install --frozen-lockfile
    $ pnpm run configure
    $ pnpm run build:all
```

2. Install via npm:

```shell
    $ npm install socketcan
```
