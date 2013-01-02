var can = require('can');
var fs = require('fs');

exports['receive_signals'] = function(test) {
	// Parse database
	var network = can.parseNetworkDescription("./tests/samples.kcd");
	var channel = can.createRawChannel("vcan0");
	var gen_channel = can.createRawChannel("vcan0");
	var db      = new can.DatabaseService(channel, network.buses["Motor"].messages);

	channel.start();
	gen_channel.start();

	var cm = { data: [ 0 ]};
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
