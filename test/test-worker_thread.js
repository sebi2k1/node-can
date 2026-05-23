'use strict';

var assert = require('assert');
var { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

// When this file is loaded as a Worker, run the receiver side.
if (!isMainThread) {
  var can = require('../dist/socketcan');
  var ch = can.createRawChannel(workerData.iface);
  ch.addListener('onMessage', function(msg) {
    parentPort.postMessage({ type: 'msg', id: msg.id, data: Array.from(msg.data) });
  });
  ch.start();
  parentPort.postMessage({ type: 'ready' });
  return;
}

// Main thread: register mocha tests.
describe('RawChannel in worker_threads', function() {
  it('should receive messages inside a worker without crashing', function(done) {
    var can = require('../dist/socketcan');
    var sender = can.createRawChannelWithOptions('vcan0', { non_block_send: true });
    sender.start();

    var worker = new Worker(__filename, { workerData: { iface: 'vcan0' } });

    worker.on('error', done);

    worker.once('message', function(msg) {
      assert.equal(msg.type, 'ready');

      sender.send({ id: 0x42, data: Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]) });

      worker.once('message', function(msg) {
        assert.equal(msg.type, 'msg');
        assert.equal(msg.id, 0x42);
        assert.deepEqual(msg.data, [0xDE, 0xAD, 0xBE, 0xEF]);
        worker.terminate();
        sender.stop();
        done();
      });
    });
  });
});
