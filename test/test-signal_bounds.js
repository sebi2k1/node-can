var assert = require('assert');

var can = require('../dist/socketcan');

// Helper: build a minimal kcd.Signal-like descriptor accepted by the Signal constructor.
function makeDesc(overrides) {
    return Object.assign({
        name:         'TestSignal',
        spn:          '',
        bitOffset:    0,
        bitLength:    8,
        endianess:    'little',
        labels:       {},
        mux:          0,
        slope:        1.0,
        intercept:    0.0,
        unit:         '',
        type:         'unsigned',
        defaultValue: 0.0,
        minValue:     undefined,
        maxValue:     undefined
    }, overrides);
}

describe('Signal bounds checking', function() {
    it('should log error when value exceeds maxValue of 0', function(done) {
        var errors = [];
        var origError = console.error;
        console.error = function() { errors.push([].slice.call(arguments).join(' ')); };

        var sig = new can.Signal(makeDesc({ maxValue: 0 }));
        sig.update(1); // 1 > 0 → should trigger error

        console.error = origError;

        assert.ok(errors.length > 0, 'Expected console.error to be called');
        assert.ok(errors[0].indexOf('out of bounds') !== -1, 'Expected out-of-bounds message');
        done();
    });

    it('should NOT log error when value equals maxValue of 0', function(done) {
        var errors = [];
        var origError = console.error;
        console.error = function() { errors.push([].slice.call(arguments).join(' ')); };

        var sig = new can.Signal(makeDesc({ maxValue: 0 }));
        sig.update(0); // 0 == 0 → no error

        console.error = origError;

        assert.strictEqual(errors.length, 0, 'Expected no console.error call');
        done();
    });

    it('should log error when value is below minValue of 0', function(done) {
        var errors = [];
        var origError = console.error;
        console.error = function() { errors.push([].slice.call(arguments).join(' ')); };

        var sig = new can.Signal(makeDesc({ minValue: 0 }));
        sig.update(-1); // -1 < 0 → should trigger error

        console.error = origError;

        assert.ok(errors.length > 0, 'Expected console.error to be called');
        assert.ok(errors[0].indexOf('out of bounds') !== -1, 'Expected out-of-bounds message');
        done();
    });

    it('should NOT log error when value equals minValue of 0', function(done) {
        var errors = [];
        var origError = console.error;
        console.error = function() { errors.push([].slice.call(arguments).join(' ')); };

        var sig = new can.Signal(makeDesc({ minValue: 0 }));
        sig.update(0); // 0 == 0 → no error

        console.error = origError;

        assert.strictEqual(errors.length, 0, 'Expected no console.error call');
        done();
    });

    it('should skip bounds check when minValue and maxValue are undefined', function(done) {
        var errors = [];
        var origError = console.error;
        console.error = function() { errors.push([].slice.call(arguments).join(' ')); };

        var sig = new can.Signal(makeDesc({}));
        sig.update(9999);

        console.error = origError;

        assert.strictEqual(errors.length, 0, 'Expected no console.error call for undefined bounds');
        done();
    });
});
