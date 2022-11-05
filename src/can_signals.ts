import can_signals = require("../build/Release/can_signals");

// Decode signal according description
// arg[0] - Data array
// arg[1] - offset zero indexed
// arg[2] - bitLength one indexed
// arg[3] - endianess
// arg[4] - signed flag
export const decodeSignal: {
	(
		data: Buffer,
		bitOffset: number,
		bitLength: number,
		endianess: boolean,
		signed: boolean
	): number[];
} = can_signals.decodeSignal;

// Encode signal according description
// arg[0] - Data array
// arg[1] - bitOffset
// arg[2] - bitLength
// arg[3] - endianess
// arg[4] - signed flag
// arg[5] - first 4 bytes value to encode
// arg[6] - second 4 bytes value to encode
export const encodeSignal: {
	(
		data: Buffer,
		bitOffset: number,
		bitLength: number,
		endianess: boolean,
		signed: boolean,
		word1: number | boolean,
		word2?: number | boolean
	): void;
} = can_signals.encodeSignal;
