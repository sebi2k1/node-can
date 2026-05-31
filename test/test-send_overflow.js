var assert = require('assert');

var can = require('../dist/socketcan');

// This test requires vcan0 to exist. If the channel cannot be created
// (no vcan0), the test is skipped gracefully.
describe('RawChannel send overflow protection', function() {
    it('should throw when send() is called with a buffer larger than 8 bytes', function(done) {
        var channel;
        try {
            channel = can.createRawChannel('vcan0');
            channel.start();
        } catch (e) {
            // vcan0 not available — skip
            this.skip();
            return;
        }

        var oversized = Buffer.alloc(16, 0xAA); // 16 bytes — well over CAN_MAX_DLEN (8)
        var msg = { id: 0x123, data: oversized };

        assert.throws(function() {
            channel.send(msg);
        }, /Data buffer exceeds CAN frame size/);

        channel.stop();
        done();
    });
});
