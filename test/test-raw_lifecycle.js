var assert = require('assert');
var childProcess = require('child_process');
var path = require('path');

describe('RawChannel restart lifecycle', function() {
    it('stops once per start and releases handles so the process can exit', function(done) {
        this.timeout(5000);
        var script = `
            const assert = require('assert');
            const can = require('./dist/socketcan');
            const channel = can.createRawChannel('vcan0');
            let stops = 0;
            let cycles = 0;
            channel.addListener('onStopped', () => {
                stops++;
                // A reentrant stop must not emit another notification.
                channel.stop();
            });
            function cycle() {
                channel.start();
                channel.stop();
                assert.strictEqual(stops, ++cycles);
                assert.throws(() => channel.stop(), /Channel not started/);
                if (cycles < 3) {
                    // Let libuv finish closing the handles before reusing them.
                    setImmediate(() => setImmediate(cycle));
                }
            }
            cycle();
        `;
        childProcess.execFile(process.execPath, ['-e', script], {
            cwd: path.resolve(__dirname, '..'),
            timeout: 3000
        }, function(error, stdout, stderr) {
            assert.ifError(error && new Error(error.message + '\n' + stdout + stderr));
            done();
        });
    });
});
