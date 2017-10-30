var signals = require('../build/Release/can_signals');
var buffer = require('buffer')

exports['little_endian_encode'] = function(test) {
	data = new Buffer([0, 0, 0, 0, 0, 0, 0, 0]);

	signals.encode_signal(data, 0, 1, true, false, 1);
console.log(data);
	signals.encode_signal(data, 1, 1, true, false, 1);
console.log(data);
	signals.encode_signal(data, 2, 1, true, false, 0 /* set zero */);
console.log(data);
	signals.encode_signal(data, 3, 1, true, false, 1);
console.log(data);
	test.deepEqual(data, new Buffer([0x0B, 0x00, 0x00, 0, 0, 0, 0, 0]));
	signals.encode_signal(data, 4, 8, true, false, 0xEA);
	test.deepEqual(data, new Buffer([0xAB, 0x0E, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 12, 12, true, false, 0xEDB);
	test.deepEqual(data, new Buffer([0xAB, 0xBE, 0xED, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 12, 12, true, false, 0);
	test.deepEqual(data, new Buffer([0xAB, 0x0E, 0x00, 0, 0, 0, 0, 0]), "Overwriting signal value failed");
	test.done();
}

exports['little_endian_decode'] = function(test) {
	data = new Buffer([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]);

	test.equals(signals.decode_signal(data, 0, 8, true, false), 0xDE);
	test.equals(signals.decode_signal(data, 0, 12, true, false), 0xDDE);
	test.equals(signals.decode_signal(data, 0, 16, true, false), 0xADDE);
	test.equals(signals.decode_signal(data, 0, 24, true, false), 0xBEADDE);
	test.equals(signals.decode_signal(data, 0, 32, true, false), 0xEFBEADDE);

	test.equals(signals.decode_signal(data, 12, 8, true, false), 0xEA);
	test.equals(signals.decode_signal(data, 12, 12, true, false), 0xBEA);
	test.equals(signals.decode_signal(data, 12, 20, true, false), 0xEFBEA);

	test.equals(signals.decode_signal(data, 0, 1, true, false), 0);
	test.equals(signals.decode_signal(data, 1, 1, true, false), 1);
	test.equals(signals.decode_signal(data, 2, 1, true, false), 1);
	test.equals(signals.decode_signal(data, 3, 1, true, false), 1);

	test.done();
}

exports['little_endian_signed_decode'] = function(test) {
	data = new Buffer([0xFE, 0xFF, 0x80]);

	test.equals(signals.decode_signal(data, 8, 8, true, true), -1);
	test.equals(signals.decode_signal(data, 0, 16, true, true), -2);
	test.equals(signals.decode_signal(data, 16, 8, true, true), -128);

	data = new Buffer([0xFF, 0xFF, 0xFF, 0xFF]);
	test.equals(signals.decode_signal(data, 0, 32, true, true), -1);

	test.done();
}

exports['little_endian_signed_encode'] = function(test) {
	data = new Buffer([0, 0, 0, 0, 0, 0, 0, 0]);

	signals.encode_signal(data, 0, 8, true, true, -1);
	test.deepEqual(data, new Buffer([0xFF, 0x00, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 0, 16, true, true, -2);
	test.deepEqual(data, new Buffer([0xFE, 0xFF, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 16, 8, true, true, -128);
	test.deepEqual(data, new Buffer([0xFE, 0xFF, 0x80, 0, 0, 0, 0, 0]));

	test.done();
}

exports['big_endian_encode'] = function(test) {
	data = new Buffer([0, 0, 0, 0, 0, 0, 0, 0]);

	signals.encode_signal(data, 0, 1, false, false, 1);
	signals.encode_signal(data, 1, 1, false, false, 1);
	signals.encode_signal(data, 2, 1, false, false, 0);
	signals.encode_signal(data, 3, 1, false, false, 1);
	test.deepEqual(data, new Buffer([0xD0, 0x00, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 4, 8, false, false, 0xEA);
	test.deepEqual(data, new Buffer([0xDE, 0xA0, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 12, 12, false, false, 0xDBE);
	test.deepEqual(data, new Buffer([0xDE, 0xAD, 0xBE, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 12, 12, false, false, 0);
	test.deepEqual(data, new Buffer([0xDE, 0xA0, 0x00, 0, 0, 0, 0, 0]), "Overwriting signal value failed");

	test.done();
}

exports['big_endian_decode'] = function(test) {
	data = new Buffer([0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE]);

	test.equals(signals.decode_signal(data, 0, 8, false, false), 0xDE);
	test.equals(signals.decode_signal(data, 0, 16, false, false), 0xDEAD);

	test.equals(signals.decode_signal(data, 7, 8, false, false), 0x56);
	test.equals(signals.decode_signal(data, 15, 16, false, false), 0xDF77);

	test.equals(signals.decode_signal(data, 0, 1, false, false), 1);
	test.equals(signals.decode_signal(data, 1, 1, false, false), 1);
	test.equals(signals.decode_signal(data, 2, 1, false, false), 0);
	test.equals(signals.decode_signal(data, 3, 1, false, false), 1);

	test.done();
}

exports['big_endian_signed_encode'] = function(test) {
	data = new Buffer([0, 0, 0, 0, 0, 0, 0, 0]);

	signals.encode_signal(data, 0, 8, false, true, -1);
	test.deepEqual(data, new Buffer([0xFF, 0x00, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 0, 16, false, true, -2);
	test.deepEqual(data, new Buffer([0xFF, 0xFE, 0x00, 0, 0, 0, 0, 0]));

	signals.encode_signal(data, 16, 8, false, true, -128);
	test.deepEqual(data, new Buffer([0xFF, 0xFE, 0x80, 0, 0, 0, 0, 0]));

	test.done();
}

exports['big_endian_signed_decode'] = function(test) {
	data = new Buffer([0xFF, 0xFE, 0x80 ]);

	test.equals(signals.decode_signal(data, 0, 8, false, true), -1);
	test.equals(signals.decode_signal(data, 0, 16, false, true), -2);
	test.equals(signals.decode_signal(data, 16, 8, false, true), -128);

	test.done();
}

