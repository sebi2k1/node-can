var assert = require('assert');
var childProcess = require('child_process');

var can = require('../dist/socketcan');

var interfaceName = 'vcan-pollerr';
var runPrivilegedTests = process.env.NODE_CAN_RUN_PRIVILEGED_TESTS === '1';
var describePrivileged = runPrivilegedTests ? describe : describe.skip;

function runIp(args, ignoreFailure) {
    var isRoot = typeof process.getuid === 'function' && process.getuid() === 0;
    var command = isRoot ? 'ip' : 'sudo';
    var commandArgs = isRoot ? args : [ '-n', 'ip' ].concat(args);

    try {
        childProcess.execFileSync(command, commandArgs, { stdio: 'pipe' });
    } catch (error) {
        if (!ignoreFailure) {
            throw error;
        }
    }
}

function createInterface() {
    runIp([ 'link', 'delete', 'dev', interfaceName ], true);
    runIp([ 'link', 'add', 'dev', interfaceName, 'type', 'vcan' ]);
    runIp([ 'link', 'set', 'dev', interfaceName, 'up' ]);
}

function deleteInterface() {
    runIp([ 'link', 'delete', 'dev', interfaceName ], true);
}

describePrivileged('RawChannel interface lifecycle', function() {
    this.timeout(5000);

    beforeEach(function() {
        createInterface();
    });

    afterEach(function() {
        deleteInterface();
    });

    it('should resume receiving after the interface goes down and comes back up', function(done) {
        var receiver = can.createRawChannel(interfaceName);
        var sender = can.createRawChannelWithOptions(interfaceName, { non_block_send: true });
        var expectedData = Buffer.from([ 0xca, 0xfe ]);
        var expectedId = 0x215;
        var finished = false;
        var timers = [];

        function later(callback, delay) {
            timers.push(setTimeout(function() {
                if (finished) {
                    return;
                }

                try {
                    callback();
                } catch (error) {
                    finish(error);
                }
            }, delay));
        }

        function finish(error) {
            if (finished) {
                return;
            }

            finished = true;
            timers.forEach(clearTimeout);

            try {
                runIp([ 'link', 'set', 'dev', interfaceName, 'up' ]);
            } catch (linkError) {
                error = error || linkError;
            }

            try { receiver.stop(); } catch (_) {}
            try { sender.stop(); } catch (_) {}
            done(error);
        }

        receiver.addListener('onStopped', function() {
            if (!finished) {
                finish(new Error('receiver stopped after a recoverable interface-down event'));
            }
        });

        sender.addListener('onStopped', function() {
            if (!finished) {
                finish(new Error('sender stopped after a recoverable interface-down event'));
            }
        });

        receiver.addListener('onMessage', function(message) {
            if (message.id !== expectedId) {
                return;
            }

            try {
                assert.deepEqual(message.data, expectedData);
                finish();
            } catch (error) {
                finish(error);
            }
        });

        receiver.start();
        sender.start();

        later(function() {
            runIp([ 'link', 'set', 'dev', interfaceName, 'down' ]);

            later(function() {
                runIp([ 'link', 'set', 'dev', interfaceName, 'up' ]);

                later(function() {
                    sender.send({ id: expectedId, data: expectedData });
                }, 200);
            }, 300);
        }, 100);

        later(function() {
            finish(new Error('timed out waiting for a frame after interface recovery'));
        }, 4000);
    });

    it('should stop after the interface is permanently removed', function(done) {
        var channel = can.createRawChannel(interfaceName);
        var finished = false;
        var timeout;

        channel.addListener('onStopped', function() {
            if (finished) {
                return;
            }

            finished = true;
            clearTimeout(timeout);
            done();
        });

        channel.start();

        setTimeout(function() {
            try {
                runIp([ 'link', 'delete', 'dev', interfaceName ]);
            } catch (error) {
                finished = true;
                clearTimeout(timeout);
                try { channel.stop(); } catch (_) {}
                done(error);
            }
        }, 100);

        timeout = setTimeout(function() {
            if (finished) {
                return;
            }

            finished = true;
            try { channel.stop(); } catch (_) {}
            done(new Error('channel did not stop after permanent interface removal'));
        }, 3000);
    });
});
