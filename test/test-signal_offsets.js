var assert = require('assert');
var signals = require('../build/Release/can_signals');

describe('signal offset bounds', function() {
    [true, false].forEach(function(littleEndian) {
        [0, 1, 2, 3].forEach(function(signalType) {
            var width = signalType === 2 ? 32 : signalType === 3 ? 64 : 8;
            var label = 'type ' + signalType + ', ' + (littleEndian ? 'LE' : 'BE');

            it('rejects invalid offsets without modifying the buffer (' + label + ')', function() {
                var data = Buffer.alloc(64, 0xa5);
                var original = Buffer.from(data);
                [-8, -1, -(2 ** 32), 2 ** 32 - 8, 2 ** 32 - 1, 2 ** 32,
                    Number.MAX_SAFE_INTEGER, 0.5, NaN, Infinity, -Infinity].forEach(function(offset) {
                    assert.throws(function() {
                        signals.decodeSignal(data, offset, width, littleEndian, signalType);
                    }, /Invalid offset/);
                    assert.throws(function() {
                        signals.encodeSignal(data, offset, width, littleEndian, signalType, 1);
                    }, /Invalid offset/);
                    assert.deepStrictEqual(data, original);
                });
            });

            [0, 8, 64].forEach(function(bytes) {
                it('checks the exact buffer boundary for ' + bytes + ' bytes (' + label + ')', function() {
                    var data = Buffer.alloc(bytes);
                    // Float widths come from the signal type, regardless of bitLength.
                    var bitLength = signalType >= 2 ? 1 : width;
                    var lastOffset = bytes * 8 - width;
                    if (bytes > 0) {
                        signals.encodeSignal(data, lastOffset, bitLength, littleEndian, signalType, 1);
                        assert.strictEqual(signals.decodeSignal(data, lastOffset, bitLength, littleEndian, signalType)[0], 1);
                    }
                    var original = Buffer.from(data);
                    var invalidOffset = bytes > 0 ? lastOffset + 1 : 0;
                    var error = bytes === 64 ? /signal extends past 64-byte frame/ : /signal extends past buffer/;
                    assert.throws(function() {
                        signals.decodeSignal(data, invalidOffset, bitLength, littleEndian, signalType);
                    }, error);
                    assert.throws(function() {
                        signals.encodeSignal(data, invalidOffset, bitLength, littleEndian, signalType, 1);
                    }, error);
                    assert.deepStrictEqual(data, original);
                });
            });
        });
    });
});
