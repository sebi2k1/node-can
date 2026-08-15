var assert = require('assert')
var fs = require('fs');

var can = require('../dist/socketcan');
var buffer = require('buffer');

function interfaceIsUp(name) {
    var flags = fs.readFileSync('/sys/class/net/' + name + '/flags', 'utf8');
    return (parseInt(flags, 16) & 1) !== 0;
}

describe('setCanBitrate', function() {
    it('should validate its arguments', function() {
        assert.throws(function() { can.setCanBitrate(); }, TypeError);
        assert.throws(function() { can.setCanBitrate(123, 125000); }, TypeError);
        assert.throws(function() { can.setCanBitrate('', 125000); }, TypeError);
        assert.throws(function() { can.setCanBitrate('can0', '125000'); }, TypeError);
        assert.throws(function() { can.setCanBitrate('can0', 125000.5); }, TypeError);
        assert.throws(function() { can.setCanBitrate('can0', NaN); }, TypeError);
        assert.throws(function() { can.setCanBitrate('can0', Infinity); }, TypeError);
        assert.throws(function() { can.setCanBitrate('can0', 999); }, RangeError);
        assert.throws(function() { can.setCanBitrate('can0', 1000001); }, RangeError);
    });

    it('should report an error for a nonexistent interface', function() {
        assert.throws(function() {
            can.setCanBitrate('non_existent_can_interface', 125000);
        }, /setCanBitrate\(non_existent_can_interface\): failed to query interface state/);
    });

    it('should not leave interfaces in a different administrative state on failure', function() {
        assert.equal(interfaceIsUp('vcan0'), true);
        assert.equal(interfaceIsUp('vcan1'), false);

        assert.throws(function() { can.setCanBitrate('vcan0', 125000); });
        assert.throws(function() { can.setCanBitrate('vcan1', 125000); });

        assert.equal(interfaceIsUp('vcan0'), true);
        assert.equal(interfaceIsUp('vcan1'), false);
    });
});

describe('RawChannel', function() {
    it('should throw exception', function(done) {
        assert.throws(function() { can.createRawChannel("non_existant_channel")});

        var channel = can.createRawChannel("vcan0");

        assert.throws(function() { channel.stop(); });

        done();
    });

    it('should be able to create channel with optional parameters', function(done) {
        var channel = can.createRawChannel("vcan0", { timestamps: true, non_block_send: true });

        assert.throws(function() { channel.stop(); });

        var channel = can.createRawChannel("vcan0");

        assert.throws(function() { channel.stop(); });

        done();
    });

    it('should call onStopped if channel is destroyed', function(done) {
        var channel = can.createRawChannel("vcan1");

        channel.addListener("onStopped", function() {
            done();
        });

        channel.start();
    });

    it('should call onStopped on stop()', function(done) {
        var channel = can.createRawChannel("vcan0");
        var stop_called = false;

        channel.addListener("onStopped", function() {
            done();
        });

        channel.start();

        setTimeout(function() { channel.stop(); }, 100);
    });

    it('should reject over-length data buffer in send()', function(done) {
        var channel = can.createRawChannel("vcan0");

        // A classic CAN frame carries at most 8 data bytes. A longer buffer must be
        // rejected before the native memcpy to avoid a stack out-of-bounds write.
        assert.throws(function() {
            channel.send({ id: 0x123, data: Buffer.alloc(64, 0x41) });
        });

        // Exactly 8 bytes is still allowed.
        assert.doesNotThrow(function() {
            channel.send({ id: 0x123, data: Buffer.alloc(8, 0x41) });
        });

        done();
    });

    it('should reject over-length data buffer in sendFD()', function(done) {
        var channel = can.createRawChannel("vcan0");

        // A CAN FD frame carries at most 64 data bytes. A longer buffer must be
        // rejected before the native memcpy to avoid a stack out-of-bounds write.
        assert.throws(function() {
            channel.sendFD({ id: 0x123, data: Buffer.alloc(128, 0x41) });
        });

        // Exactly 64 bytes is still allowed.
        assert.doesNotThrow(function() {
            channel.sendFD({ id: 0x123, data: Buffer.alloc(64, 0x41) });
        });

        done();
    });

    it('should receive 100 messages', function(done) {
        var c1 = can.createRawChannelWithOptions("vcan0", { timestamps: true });
        var c2 = can.createRawChannelWithOptions("vcan0", { non_block_send: true });

        c1.start();
        c2.start();

        var canmsg = { id: 10, data: Buffer.from([ 0, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF7 ]) };

        var rx_count = 0;

        c1.addListener("onMessage", function(msg) {
            assert.equal(msg.data[0], rx_count);
            assert.ok(msg.ts_sec !== undefined)
            rx_count++;
        });

        // Generate 100 messages
        for (var i = 0; i < 100; i++) {
            canmsg.data[0] = i;
            c2.send(canmsg);
        }

        // Check after 100ms if all 100 messages have been received
        setTimeout(function() {
            assert.equal(rx_count, i);
            c1.stop();
            c2.stop();

            done();
        }, 100);
    });
});
