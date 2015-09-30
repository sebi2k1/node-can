var can = require('./socketcan.js');
console.log("started");
var channel = can.createRawChannel("vcan0", false);
console.log("constructed");
// Log any message
channel.addListener("onMessage", function(msg) { console.log(msg); } );
// reply
channel.addListener("onMessage", channel.send, channel);
console.log("listening");
channel.start();
console.log("started");


