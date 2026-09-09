'use strict';

var assert = require('assert');
var can = require('../dist/socketcan');

function signal(name, offset, length, endianess, type, mux) {
    return new can.kcd.Signal(name, '', offset, length, endianess, {}, mux || 0,
        1, 0, '', type || 'unsigned');
}

function fixture(signals, mux) {
    var receive;
    var channel = {
        addListener: function(event, callback, receiver) {
            assert.strictEqual(event, 'onMessage');
            receive = callback.bind(receiver);
        }
    };
    var desc = new can.kcd.Message('Test', 0x123, false, false, 8, 0, !!mux, mux);
    desc.signals = signals;
    var db = new can.DatabaseService(channel, { messages: [desc] });
    var message = db.messages.Test;
    var events = [];
    Object.values(message.signals).forEach(function(s) {
        s.onUpdate(function() { events.push(s.name + ':update'); });
        s.onChange(function() { events.push(s.name + ':change'); });
    });
    message.onMessageUpdate(function() { events.push('message'); });
    return {
        message: message,
        events: events,
        receive: function(data) { receive({ id: desc.id, data: data }); }
    };
}

describe('DatabaseService received payload bounds', function() {
    ['little', 'big'].forEach(function(endianess) {
        it('rejects a truncated frame atomically and accepts the next valid frame (' + endianess + ')', function() {
            var f = fixture([
                signal('First', 0, 8, endianess),
                signal('Last', 15, 2, endianess)
            ]);
            f.receive(Buffer.from([1, 0xff, 0xff]));
            assert.strictEqual(f.message.signals.First.value, 1);
            assert.strictEqual(f.message.signals.Last.value, 3);
            f.events.length = 0;

            [Buffer.alloc(0), Buffer.from([2, 0])].forEach(function(data) {
                assert.doesNotThrow(function() { f.receive(data); });
                assert.strictEqual(f.message.signals.First.value, 1);
                assert.strictEqual(f.message.signals.Last.value, 3);
                assert.deepStrictEqual(f.events, []);
            });

            f.receive(Buffer.from([2, 0, 0]));
            assert.strictEqual(f.message.signals.First.value, 2);
            assert.strictEqual(f.message.signals.Last.value, 0);
            assert.deepStrictEqual(f.events, [
                'First:update', 'First:change', 'Last:update', 'Last:change', 'message'
            ]);
        });

        ['single', 'double'].forEach(function(type) {
            it('checks the native fixed width for ' + type + ' signals (' + endianess + ')', function() {
                var bytes = type === 'single' ? 4 : 8;
                var f = fixture([signal('Float', 0, 1, endianess, type)]);
                assert.doesNotThrow(function() { f.receive(Buffer.alloc(bytes - 1)); });
                assert.strictEqual(f.message.signals.Float.value, undefined);
                assert.deepStrictEqual(f.events, []);

                f.receive(Buffer.alloc(bytes));
                assert.strictEqual(f.message.signals.Float.value, 0);
                assert.deepStrictEqual(f.events, ['Float:update', 'Float:change', 'message']);
            });
        });
    });

    it('rejects a truncated multiplexer before decoding it', function() {
        var f = fixture([signal('Value', 0, 8, 'little')], new can.kcd.Mux('Mux', 8, 1));
        assert.doesNotThrow(function() { f.receive(Buffer.from([42])); });
        assert.strictEqual(f.message.signals.Value.value, undefined);
        assert.deepStrictEqual(f.events, []);

        f.receive(Buffer.from([42, 0]));
        assert.strictEqual(f.message.signals.Value.value, 42);
        assert.deepStrictEqual(f.events, ['Value:update', 'Value:change', 'message']);
    });

    it('checks only the selected multiplexed signals', function() {
        var f = fixture([
            signal('Short', 8, 8, 'little', 'unsigned', 0),
            signal('Long', 16, 8, 'little', 'unsigned', 1)
        ], new can.kcd.Mux('Mux', 0, 1));

        f.receive(Buffer.from([0, 42]));
        assert.strictEqual(f.message.signals.Short.value, 42);
        assert.strictEqual(f.message.signals.Long.value, undefined);
        f.events.length = 0;

        assert.doesNotThrow(function() { f.receive(Buffer.from([1, 42])); });
        assert.strictEqual(f.message.signals.Long.value, undefined);
        assert.deepStrictEqual(f.events, []);

        f.receive(Buffer.from([1, 42, 43]));
        assert.strictEqual(f.message.signals.Long.value, 43);
        assert.deepStrictEqual(f.events, ['Long:update', 'Long:change', 'message']);
    });
});
