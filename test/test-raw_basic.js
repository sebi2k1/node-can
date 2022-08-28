var assert = require('assert')

var can = require('../dist/socketcan');
var buffer = require('buffer');

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
