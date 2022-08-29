var assert = require('assert')

var can = require('../dist/socketcan');

describe('Parsing KCD', function() {
    it('should parse consumer-definition incl. references', function(done) {
        // Parse database
        var network = can.parseNetworkDescription("./test/samples.kcd");

        assert.equal(network.nodes['12'].buses['Motor'].consumes[0].id, 895);

        const motorBus = network.buses['Motor'];
        const cruiseControlStatus = motorBus.messages[0];
        console.log('ccs', cruiseControlStatus);
        assert.equal(cruiseControlStatus.signals[0].value.type, 'signed');
        assert.equal(cruiseControlStatus.signals[1].value.type, 'unsigned');
        done();
    });
    it('should parse the first mux definition', function(done) {
        // Parse database
        var network = can.parseNetworkDescription("./test/samples.kcd");

        assert.equal(network.buses['OBD2'].messages[0].signals[0].mux, 8)
        assert.equal(network.buses['OBD2'].messages[0].muxed, true)
        assert.equal(network.buses['OBD2'].messages[0].name, 'OBD2')
        assert.equal(network.buses['OBD2'].messages[0].mux.name, 'service')

        done();
    });
});
