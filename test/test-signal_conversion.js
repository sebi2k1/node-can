var assert = require('assert');

var signals = require('../build/Release/can_signals');
var buffer = require('buffer');

describe('signals', function() {
    it('should properly encode', function(done) {
        data = Buffer.from([0, 0, 0, 0, 0, 0, 0, 0]);

        signals.encodeSignal(data, 0, 1, true, false, 1);
        signals.encodeSignal(data, 1, 1, true, false, 1);
        signals.encodeSignal(data, 2, 1, true, false, 0 /* set zero */);
        signals.encodeSignal(data, 3, 1, true, false, 1);
        assert.deepEqual(data, Buffer.from([0x0B, 0x00, 0x00, 0, 0, 0, 0, 0]));
        signals.encodeSignal(data, 4, 8, true, false, 0xEA);
        assert.deepEqual(data, Buffer.from([0xAB, 0x0E, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 12, 12, true, false, 0xEDB);
        assert.deepEqual(data, Buffer.from([0xAB, 0xBE, 0xED, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 12, 12, true, false, 0);
        assert.deepEqual(data, Buffer.from([0xAB, 0x0E, 0x00, 0, 0, 0, 0, 0]), "Overwriting signal value failed");

        signals.encodeSignal(data, 0, 64, true, false, 0xEFBEADDE, 0xBEBAFECA);
        assert.deepEqual(data, Buffer.from([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]));

        done();
    });

    it('should properly decode little endian', function(done) {
        data = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]);

        assert.deepEqual(signals.decodeSignal(data, 0, 8, true, false), [0xDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 12, true, false), [0xDDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 16, true, false), [0xADDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 24, true, false), [0xBEADDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, true, false), [0xEFBEADDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 64, true, false), [0xEFBEADDE, 0xBEBAFECA]);

        assert.deepEqual(signals.decodeSignal(data, 12, 8, true, false), [0xEA, 0]);
        assert.deepEqual(signals.decodeSignal(data, 12, 12, true, false), [0xBEA, 0]);
        assert.deepEqual(signals.decodeSignal(data, 12, 20, true, false), [0xEFBEA, 0]);

        assert.deepEqual(signals.decodeSignal(data, 0, 1, true, false), [0, 0]);
        assert.deepEqual(signals.decodeSignal(data, 1, 1, true, false), [1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 2, 1, true, false), [1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 3, 1, true, false), [1, 0]);

        done();
    });

    it('should properly decode signed little endian', function(done) {
        data = Buffer.from([0xFE, 0xFF, 0x80]);

        assert.deepEqual(signals.decodeSignal(data, 8, 8, true, true), [-1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 16, true, true), [-2, 0]);
        assert.deepEqual(signals.decodeSignal(data, 16, 8, true, true), [-128, 0]);

        data = Buffer.from([0xFF, 0xFF, 0xFF, 0xFF]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, true, true), [-1, 0]);

        done();
    });

    it('should properly encode signed little endian', function(done) {
        data = Buffer.from([0, 0, 0, 0, 0, 0, 0, 0]);

        signals.encodeSignal(data, 0, 8, true, true, -1);
        assert.deepEqual(data, Buffer.from([0xFF, 0x00, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 0, 16, true, true, -2);
        assert.deepEqual(data, Buffer.from([0xFE, 0xFF, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 16, 8, true, true, -128);
        assert.deepEqual(data, Buffer.from([0xFE, 0xFF, 0x80, 0, 0, 0, 0, 0]));

        done();
    });

    it('should properly encode big endian', function(done) {
        data = Buffer.from([0, 0, 0, 0, 0, 0, 0, 0]);

        signals.encodeSignal(data, 0, 1, false, false, 1);
        signals.encodeSignal(data, 1, 1, false, false, 1);
        signals.encodeSignal(data, 2, 1, false, false, 0);
        signals.encodeSignal(data, 3, 1, false, false, 1);
        assert.deepEqual(data, Buffer.from([0xD0, 0x00, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 4, 8, false, false, 0xEA);
        assert.deepEqual(data, Buffer.from([0xDE, 0xA0, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 12, 12, false, false, 0xDBE);
        assert.deepEqual(data, Buffer.from([0xDE, 0xAD, 0xBE, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 12, 12, false, false, 0);
        assert.deepEqual(data, Buffer.from([0xDE, 0xA0, 0x00, 0, 0, 0, 0, 0]), "Overwriting signal value failed");

        signals.encodeSignal(data, 0, 64, false, false, 0xCAFEBABE, 0xDEADBEEF);
        assert.deepEqual(data, Buffer.from([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]));

        done();
    });

    it('should properly decode big endian', function(done) {
        data = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]);

        assert.deepEqual(signals.decodeSignal(data, 0, 8, false, false), [0xDE, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 16, false, false), [0xDEAD, 0]);

        assert.deepEqual(signals.decodeSignal(data, 7, 8, false, false), [0x56, 0]);
        assert.deepEqual(signals.decodeSignal(data, 15, 16, false, false), [0xDF77, 0]);

        assert.deepEqual(signals.decodeSignal(data, 0, 1, false, false), [1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 1, 1, false, false), [1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 2, 1, false, false), [0, 0]);
        assert.deepEqual(signals.decodeSignal(data, 3, 1, false, false), [1, 0]);

        done();
    });

    it('should properly encode signed big endian', function(done) {
        data = Buffer.from([0, 0, 0, 0, 0, 0, 0, 0]);

        signals.encodeSignal(data, 0, 8, false, true, -1);
        assert.deepEqual(data, Buffer.from([0xFF, 0x00, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 0, 16, false, true, -2);
        assert.deepEqual(data, Buffer.from([0xFF, 0xFE, 0x00, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 16, 8, false, true, -128);
        assert.deepEqual(data, Buffer.from([0xFF, 0xFE, 0x80, 0, 0, 0, 0, 0]));

        signals.encodeSignal(data, 16, 16, false, true, -32767);
        assert.deepEqual(data, Buffer.from([0xFF, 0xFE, 0x80, 0x01, 0, 0, 0, 0]));

        // signals.encodeSignal(data, 0, 64, false, true, -9223372036);
        // test.deepEqual(data, Buffer.from([0xFF, 0xFF, 0xFF, 0xFD, 0xDA, 0x3E, 0x82, 0xFB]));

        done();
    });

    it('should properly decode signed big endian', function(done) {
        data = Buffer.from([0xFF, 0xFE, 0x80, 0x01]);

        assert.deepEqual(signals.decodeSignal(data, 0, 8, false, true), [-1, 0]);
        assert.deepEqual(signals.decodeSignal(data, 0, 16, false, true), [-2, 0]);
        assert.deepEqual(signals.decodeSignal(data, 16, 8, false, true), [-128, 0]);
        assert.deepEqual(signals.decodeSignal(data, 16, 16, false, true), [-32767, 0]);

        // data = Buffer.from([0xFF, 0xFF, 0xFF, 0xFD, 0xDA, 0x3E, 0x82, 0xFB]);
        // test.deepEqual(signals.decodeSignal(data, 0, 64, false, true), [-9223372037, 0]);

        done();
    });
});
