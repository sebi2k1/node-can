var can = require('socketcan');
var buffer = require('buffer');

exports['channel_creation'] = function(test) {
	// Throw exception if channel doesn't exist
	test.throws(function() { can.createRawChannel("non_existant_channel")});
	
	var channel = can.createRawChannel("vcan0");
	
	test.throws(function() { channel.stop(); });
	
	test.done();
}

// Send 100 messages from c2 to c1
exports['rxtx_test'] = function(test) {
	var c1 = can.createRawChannel("vcan0");
	var c2 = can.createRawChannel("vcan0");
	
	c1.start();
	c2.start();
	
	var canmsg = { id: 10, data: new Buffer([ 0, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF7 ]) };

	var rx_count = 0;
	
	c1.addListener("onMessage", function(msg) {
		test.equal(msg.data[0], rx_count);
		rx_count++;
	});

	for (var i = 0; i < 100; i++) {
		canmsg.data[0] = i;	
		c2.send(canmsg);
	}
	
	setTimeout(function() {
		test.equals(rx_count, i);
		c1.stop();
		c2.stop();
		
		test.done();
	}, 100);
}
