var can = require('socketcan');
var fs = require('fs');
var buffer = require('buffer');

exports['receive_signals'] = function(test) {
	// Parse database
	var network = can.parseNetworkDescription("./tests/samples.kcd");
	var channel = can.createRawChannel("vcan0");
	var gen_channel = can.createRawChannel("vcan0");
	var db      = new can.DatabaseService(channel, network.buses["Motor"]);

	channel.start();
	gen_channel.start();

	var cm = { data: new Buffer([ 0 ]) };
	cm.id = db.messages["CruiseControlStatus"].id;

	var next_speed = 255;
	var expected = -1; /* 255 = 0xFF = -1 in 2's complement */
	cm.data[0] = next_speed;
	gen_channel.send(cm);

	db.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
		test.equal(s.value, expected);

		next_speed = s.value - 1;
		if (expected < 0)
		{
			expected--;

			if (expected < -128)
				expected = 127;
		}
		else
		{
			expected--;
		}

		if (expected == 0) {
			channel.stop();
			gen_channel.stop();
			test.done();
		} else {
			cm.data[0] = next_speed;
			gen_channel.send(cm);
		}
	});
}

exports['transmit_receive_signals'] = function(test) {
	// Parse database
	var network = can.parseNetworkDescription("./tests/samples.kcd");

	var channel = can.createRawChannel("vcan0");
	var gen_channel = can.createRawChannel("vcan0");

	var rx_db = new can.DatabaseService(channel, network.buses["Motor"]);
	var tx_db = new can.DatabaseService(gen_channel, network.buses["Motor"]);

	channel.start();
	gen_channel.start();

	var next_speed = -128;

	tx_db.messages["CruiseControlStatus"].signals["SpeedKm"].update(next_speed);
	tx_db.send("CruiseControlStatus");

	rx_db.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
		test.equal(s.value, next_speed++);

		if (next_speed > 127) {
			channel.stop();
			gen_channel.stop();
			test.done();
		} else {
			tx_db.messages["CruiseControlStatus"].signals["SpeedKm"].update(next_speed);
			tx_db.send("CruiseControlStatus");
		}
	});
}

