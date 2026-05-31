var assert = require('assert');

var signals = require('../build/Release/can_signals');
var buffer = require('buffer');

// SIGNAL_TYPE constants (mirror the native enum in signals.cc)
var SIGNAL_UNSIGNED = 0;
var SIGNAL_SIGNED   = 1;
var SIGNAL_FLOAT32  = 2;
var SIGNAL_FLOAT64  = 3;

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

    it('should encode float32 little endian', function(done) {
        // 1.0f = 0x3F800000 → LE bytes [0x00, 0x00, 0x80, 0x3F]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 32, true, SIGNAL_FLOAT32, 1.0);
        assert.deepEqual(data, Buffer.from([0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00]));

        // -1.0f = 0xBF800000 → LE bytes [0x00, 0x00, 0x80, 0xBF]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 32, true, SIGNAL_FLOAT32, -1.0);
        assert.deepEqual(data, Buffer.from([0x00, 0x00, 0x80, 0xBF, 0x00, 0x00, 0x00, 0x00]));

        // float at bit offset 32 (bytes 4–7)
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 32, 32, true, SIGNAL_FLOAT32, 1.0);
        assert.deepEqual(data, Buffer.from([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F]));

        done();
    });

    it('should decode float32 little endian', function(done) {
        // 1.0f LE bytes [0x00, 0x00, 0x80, 0x3F]
        data = Buffer.from([0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, true, SIGNAL_FLOAT32), [1.0, 0]);

        // -1.0f LE bytes [0x00, 0x00, 0x80, 0xBF]
        data = Buffer.from([0x00, 0x00, 0x80, 0xBF, 0x00, 0x00, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, true, SIGNAL_FLOAT32), [-1.0, 0]);

        // float at bit offset 32
        data = Buffer.from([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F]);
        assert.deepEqual(signals.decodeSignal(data, 32, 32, true, SIGNAL_FLOAT32), [1.0, 0]);

        done();
    });

    it('should encode float32 big endian', function(done) {
        // 1.0f = 0x3F800000 → BE bytes [0x3F, 0x80, 0x00, 0x00]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 32, false, SIGNAL_FLOAT32, 1.0);
        assert.deepEqual(data, Buffer.from([0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]));

        // -1.0f = 0xBF800000 → BE bytes [0xBF, 0x80, 0x00, 0x00]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 32, false, SIGNAL_FLOAT32, -1.0);
        assert.deepEqual(data, Buffer.from([0xBF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]));

        // float at bit offset 32 (bytes 4–7)
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 32, 32, false, SIGNAL_FLOAT32, 1.0);
        assert.deepEqual(data, Buffer.from([0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00]));

        done();
    });

    it('should decode float32 big endian', function(done) {
        // 1.0f BE bytes [0x3F, 0x80, 0x00, 0x00]
        data = Buffer.from([0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, false, SIGNAL_FLOAT32), [1.0, 0]);

        // -1.0f BE bytes [0xBF, 0x80, 0x00, 0x00]
        data = Buffer.from([0xBF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 0, 32, false, SIGNAL_FLOAT32), [-1.0, 0]);

        // float at bit offset 32
        data = Buffer.from([0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 32, 32, false, SIGNAL_FLOAT32), [1.0, 0]);

        done();
    });

    it('should round-trip float32 for values not exactly representable', function(done) {
        const val = Math.fround(3.14); // exact float32 representation
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 32, true, SIGNAL_FLOAT32, val);
        const result = signals.decodeSignal(data, 0, 32, true, SIGNAL_FLOAT32);
        assert.strictEqual(result[0], val);
        done();
    });

    it('should encode float64 little endian', function(done) {
        // 1.0 double = 0x3FF0000000000000 → LE bytes [0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0x3F]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 64, true, SIGNAL_FLOAT64, 1.0);
        assert.deepEqual(data, Buffer.from([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F]));

        done();
    });

    it('should decode float64 little endian', function(done) {
        data = Buffer.from([0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F]);
        assert.deepEqual(signals.decodeSignal(data, 0, 64, true, SIGNAL_FLOAT64), [1.0, 0]);

        done();
    });

    it('should encode float64 big endian', function(done) {
        // 1.0 double BE bytes [0x3F,0xF0,0x00,0x00,0x00,0x00,0x00,0x00]
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 64, false, SIGNAL_FLOAT64, 1.0);
        assert.deepEqual(data, Buffer.from([0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]));

        done();
    });

    it('should decode float64 big endian', function(done) {
        data = Buffer.from([0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
        assert.deepEqual(signals.decodeSignal(data, 0, 64, false, SIGNAL_FLOAT64), [1.0, 0]);

        done();
    });

    it('should round-trip float64', function(done) {
        const val = 3.141592653589793;
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 64, true, SIGNAL_FLOAT64, val);
        const result = signals.decodeSignal(data, 0, 64, true, SIGNAL_FLOAT64);
        assert.strictEqual(result[0], val);
        done();
    });

    it('should round-trip float64 with negative value', function(done) {
        const val = -2.718281828459045;
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 64, true, SIGNAL_FLOAT64, val);
        assert.strictEqual(signals.decodeSignal(data, 0, 64, true, SIGNAL_FLOAT64)[0], val);
        // big-endian too
        data = Buffer.alloc(8);
        signals.encodeSignal(data, 0, 64, false, SIGNAL_FLOAT64, val);
        assert.strictEqual(signals.decodeSignal(data, 0, 64, false, SIGNAL_FLOAT64)[0], val);
        done();
    });

    it('should handle IEEE-754 special values for float32', function(done) {
        for (const val of [Infinity, -Infinity, NaN]) {
            data = Buffer.alloc(8);
            signals.encodeSignal(data, 0, 32, true, SIGNAL_FLOAT32, val);
            const decoded = signals.decodeSignal(data, 0, 32, true, SIGNAL_FLOAT32)[0];
            if (isNaN(val)) {
                assert.ok(isNaN(decoded), 'NaN should round-trip as NaN');
            } else {
                assert.strictEqual(decoded, val);
            }
        }
        done();
    });

    it('should handle IEEE-754 special values for float64', function(done) {
        for (const val of [Infinity, -Infinity, NaN]) {
            data = Buffer.alloc(8);
            signals.encodeSignal(data, 0, 64, true, SIGNAL_FLOAT64, val);
            const decoded = signals.decodeSignal(data, 0, 64, true, SIGNAL_FLOAT64)[0];
            if (isNaN(val)) {
                assert.ok(isNaN(decoded), 'NaN should round-trip as NaN');
            } else {
                assert.strictEqual(decoded, val);
            }
        }
        done();
    });

    it('should decode correctly when wrong bitLength is passed for float32', function(done) {
        // effectiveBitLength override: even if caller passes 16, we should read 32 bits
        data = Buffer.from([0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00]);
        const result = signals.decodeSignal(data, 0, 16 /* wrong */, true, SIGNAL_FLOAT32);
        assert.strictEqual(result[0], 1.0);
        done();
    });
});
