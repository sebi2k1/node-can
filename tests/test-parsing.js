var can = require('socketcan');

exports['test_sample_consumes'] = function(test) {
	// Parse database
	var network = can.parseNetworkDescription("./tests/samples.kcd");

	test.equals(network.buses['OBD2'].messages[0].signals[0].mux, 0)
	test.equals(network.buses['OBD2'].messages[0].muxed, true)
	test.equals(network.buses['OBD2'].messages[0].name, 'OBD2')

	test.equals(network.nodes['12'].buses['Motor'].consumes[0].id, 895);

	test.done();
}
