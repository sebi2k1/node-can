var can = require('socketcan');

exports['test_sample_consumes'] = function(test) {
	// Parse database
	var network = can.parseNetworkDescription("./tests/samples.kcd");

	test.equals(network.nodes['12'].buses['Motor'].consumes[0].id, 895);

	test.done();
}
