var can = require('socketcan');
var buffer = require('buffer');

exports['channel_creation'] = function(test) {
	// Throw exception if channel doesn't exist
	test.throws(function() { can.createRawChannel("non_existant_channel")});

	var channel = can.createRawChannel("vcan0");

	test.throws(function() { channel.stop(); });

	test.done();
}

exports['channel_creation_w_options'] = function(test) {
	var channel = can.createRawChannel("vcan0", { timestamps: true, non_block_send: true });

	test.throws(function() { channel.stop(); });

	var channel = can.createRawChannel("vcan0");

	test.throws(function() { channel.stop(); });

	test.done();
}

exports['channel_error'] = function(test) {
	var channel = can.createRawChannel("vcan1");

	channel.addListener("onStopped", function() {
		test.done();
	});

	channel.start();
}

exports['channel_stopped'] = function(test) {
	var channel = can.createRawChannel("vcan0");
	var stop_called = false;

	channel.addListener("onStopped", function() {
		stop_called = true
	});
	
	channel.start();
	
	setTimeout(function() { channel.stop(); }, 100);

	setTimeout(function() {
		test.ok(stop_called)
		test.done()
	}, 100);
}

// Send 100 messages from c2 to c1
exports['rxtx_test'] = function(test) {
	var c1 = can.createRawChannelWithOptions("vcan0", { timestamps: true });
	var c2 = can.createRawChannelWithOptions("vcan0", { non_block_send: true });

	c1.start();
	c2.start();

	var canmsg = { id: 10, data: Buffer.from([ 0, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF7 ]) };

	var rx_count = 0;

	c1.addListener("onMessage", function(msg) {
		test.equal(msg.data[0], rx_count);
		test.ok(msg.ts_sec !== undefined)
		rx_count++;
	});

	// Generate 100 messages
	for (var i = 0; i < 100; i++) {
		canmsg.data[0] = i;
		c2.send(canmsg);
	}

	// Check after 100ms if all 100 messages have been received
	setTimeout(function() {
		test.equals(rx_count, i);
		c1.stop();
		c2.stop();

		test.done();
	}, 100);
}
