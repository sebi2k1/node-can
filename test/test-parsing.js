var assert = require('assert')

var can = require('../dist/socketcan');

describe('Parsing KCD', function() {
    it('should parse consumer-definition incl. references', function(done) {
        // Parse database
        var network = can.parseNetworkDescription("./test/samples.kcd");

        assert.equal(network.nodes['12'].buses['Motor'].consumes[0].id, 895);

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
