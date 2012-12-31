var can = require('can');
var fs = require('fs');

// Parse database
var network = can.parseNetworkDescription("./can_definition_sample.kcd");

var node = network.nodes['16'];

console.log("Simulating " + node.name);

var i = 0;
for (b in node.buses) {
	var c = can.createRawChannel("vcan" + i++);
	var d = new can.DatabaseService(c, network.buses[b].messages);
	
	c.start();

	console.log("Starting simulation " + b);

	// Generate all CAN message this node is the producer
	for (m in node.buses[b].produces) {
		var msg = node.buses[b].produces[m];
		
		if (msg.interval)
			setInterval(function(name) { d.send(name); }, msg.interval, msg.name);
	}
}
