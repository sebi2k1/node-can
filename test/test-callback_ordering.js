'use strict';

// Regression test for the napi_make_callback fix (4.1.0).
//
// The old N-API implementation called fn.Call() → napi_call_function(), which
// does NOT run a microtask checkpoint between callbacks.  The NaN
// implementation used Nan::Callback::Call() → node::MakeCallback(), which
// DOES run a microtask checkpoint (and fires async hooks) after each call.
//
// Without the checkpoint, adapters that queue work via Promises or
// setImmediate inside onMessage fall behind: when 100 frames arrive in one
// uv_async batch, all 100 JS callbacks fire back-to-back with no chance for
// Promise continuations to run, overwhelming any async processing queue.
//
// This test sends a batch of frames (fewer than MAX_FRAMES_PER_ASYNC_EVENT so
// they all land in one uv_async invocation) and verifies that the microtask
// scheduled by callback N has resolved before callback N+1 fires.

var assert = require('assert');
var can    = require('../dist/socketcan');

describe('RawChannel callback ordering', function() {
    it('should drain microtasks between consecutive onMessage callbacks', function(done) {
        var c_rx = can.createRawChannelWithOptions('vcan0', {});
        var c_tx = can.createRawChannelWithOptions('vcan0', { non_block_send: true });

        c_rx.start();
        c_tx.start();

        var syncCount      = 0;    // incremented synchronously in each callback
        var asyncCount     = 0;    // incremented by a resolved Promise (microtask)
        var interleavingOk = true;
        var TOTAL          = 30;   // safely below MAX_FRAMES_PER_ASYNC_EVENT (100)

        c_rx.addListener('onMessage', function(msg) {
            // With napi_make_callback a microtask checkpoint runs after every
            // callback, so the Promise queued by the previous callback has
            // already resolved when we get here.  asyncCount must therefore
            // equal syncCount for every call after the first.
            if (syncCount > 0 && asyncCount !== syncCount) {
                interleavingOk = false;
            }
            syncCount++;
            // Microtask: resolved Promise continuation.
            Promise.resolve().then(function() { asyncCount++; });
        });

        var frame = { id: 0x200, data: Buffer.alloc(1) };
        for (var i = 0; i < TOTAL; i++) {
            frame.data[0] = i & 0xFF;
            c_tx.send(frame);
        }

        setTimeout(function() {
            c_rx.stop();
            c_tx.stop();

            assert.strictEqual(syncCount, TOTAL, 'should receive all frames synchronously');
            assert.strictEqual(asyncCount, TOTAL, 'all Promise continuations should have run');
            assert.ok(
                interleavingOk,
                'napi_make_callback must drain microtasks between callbacks; ' +
                'got asyncCount=' + asyncCount + ' syncCount=' + syncCount
            );
            done();
        }, 500);
    });

    it('should drain process.nextTick between consecutive onMessage callbacks', function(done) {
        var c_rx = can.createRawChannelWithOptions('vcan0', {});
        var c_tx = can.createRawChannelWithOptions('vcan0', { non_block_send: true });

        c_rx.start();
        c_tx.start();

        var syncCount      = 0;
        var tickCount      = 0;
        var interleavingOk = true;
        var TOTAL          = 30;

        c_rx.addListener('onMessage', function(msg) {
            // process.nextTick callbacks are drained before Promise microtasks;
            // they must also run between callbacks when napi_make_callback is used.
            if (syncCount > 0 && tickCount !== syncCount) {
                interleavingOk = false;
            }
            syncCount++;
            process.nextTick(function() { tickCount++; });
        });

        var frame = { id: 0x201, data: Buffer.alloc(1) };
        for (var i = 0; i < TOTAL; i++) {
            frame.data[0] = i & 0xFF;
            c_tx.send(frame);
        }

        setTimeout(function() {
            c_rx.stop();
            c_tx.stop();

            assert.strictEqual(syncCount, TOTAL, 'should receive all frames synchronously');
            assert.strictEqual(tickCount,  TOTAL, 'all nextTick callbacks should have run');
            assert.ok(
                interleavingOk,
                'process.nextTick must run between callbacks; ' +
                'got tickCount=' + tickCount + ' syncCount=' + syncCount
            );
            done();
        }, 500);
    });
});
