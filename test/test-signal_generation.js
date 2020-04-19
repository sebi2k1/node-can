var assert = require('assert');

var can = require('socketcan');
var fs = require('fs');
var buffer = require('buffer');

describe('Database', function() {
    var network = undefined;
    var channel = undefined;
    var gen_channel = undefined;

    beforeEach(function(done) {
        network = can.parseNetworkDescription("./test/samples.kcd");
        channel = can.createRawChannel("vcan0");
        gen_channel = can.createRawChannel("vcan0");

        channel.start();
        gen_channel.start();

        done();
    });

    afterEach(function(done) {
        channel.stop();
        gen_channel.stop();

        done();
    });

    it('should properly receive and decode signal', function(done) {
        var db = new can.DatabaseService(channel, network.buses["Motor"]);

        var cm = { data: Buffer.from([ 0 ]) };
        cm.id = db.messages["CruiseControlStatus"].id;

        var next_speed = 255;
        var expected = -1; /* 255 = 0xFF = -1 in 2's complement */
        cm.data[0] = next_speed;
        gen_channel.send(cm);

        db.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
            assert.equal(s.value, expected);

            next_speed = s.value - 1;
            if (expected < 0)
            {
                expected--;

                if (expected < -128)
                    expected = 127;
            }
            else
            {
                expected--;
            }

            if (expected == 0) {
                done();
            } else {
                cm.data[0] = next_speed;
                gen_channel.send(cm);
            }
        });
    });

    it('should call onUpdate', function(done) {
        var db = new can.DatabaseService(channel, network.buses["Motor"]);

        var cm = { data: Buffer.from([ 0 ]) };
        cm.id = db.messages["CruiseControlStatus"].id;

        var next_speed = 255;
        var expected = -1; /* 255 = 0xFF = -1 in 2's complement */
        var counter = 0;
        cm.data[0] = next_speed;
        gen_channel.send(cm);

        db.messages["CruiseControlStatus"].signals["SpeedKm"].onUpdate(function(s) {
            assert.equal(s.value, expected);
            if (counter >= 1) {
                done();
            }
            else {
                counter++;
                gen_channel.send(cm);
            }
        });
    });

    it('should receive a transmitted signal', function(done) {
        var rx_db = new can.DatabaseService(channel, network.buses["Motor"]);
        var tx_db = new can.DatabaseService(gen_channel, network.buses["Motor"]);

        var next_speed = -128;

        tx_db.messages["CruiseControlStatus"].signals["SpeedKm"].update(next_speed);
        tx_db.send("CruiseControlStatus");

        rx_db.messages["CruiseControlStatus"].signals["SpeedKm"].onChange(function(s) {
            assert.equal(s.value, next_speed++);

            if (next_speed > 127) {
                done();
            } else {
                tx_db.messages["CruiseControlStatus"].signals["SpeedKm"].update(next_speed);
                tx_db.send("CruiseControlStatus");
            }
        });
    });

    it('should receive a transmitted muxed signal', function(done) {
        var rx_db = new can.DatabaseService(channel, network.buses["OBD2"]);
        var tx_db = new can.DatabaseService(gen_channel, network.buses["OBD2"]);

        var next_speed = 0;

        tx_db.messages["OBD2"].signals["S1_PID_00_PIDsSupported_01_20"].update(next_speed);
        tx_db.send("OBD2.8");

        rx_db.messages["OBD2"].signals["S1_PID_00_PIDsSupported_01_20"].onChange(function(s) {
            assert.equal(s.value, next_speed++);

            if (next_speed > 127) {
                done();
            } else {
                tx_db.messages["OBD2"].signals["S1_PID_00_PIDsSupported_01_20"].update(next_speed);
                tx_db.send("OBD2.8");
            }
        });
    });
});