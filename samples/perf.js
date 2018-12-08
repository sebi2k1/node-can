var can = require('socketcan');

var channel = can.createRawChannel("vcan0", true);

var c_per_s = 0;

// Log any message
channel.addListener("onMessage", function(msg) {
	 var a=msg.id;
c_per_s++;
} );

setInterval(function() {
console.log(c_per_s);
c_per_s = 0;
}, 1000);

channel.start();
