declare module "*can_signals.node" {
	// Signal type constants passed as the 5th argument.
	// Accepts a boolean for backward compatibility (false=unsigned, true=signed).
	// 0 = unsigned, 1 = signed, 2 = float32 (single), 3 = float64 (double)
	type SignalType = boolean | 0 | 1 | 2 | 3;

	// Decode signal according description
	// arg[0] - Data array
	// arg[1] - offset zero indexed
	// arg[2] - bitLength one indexed
	// arg[3] - endianess (true = Intel/LE, false = Motorola/BE)
	// arg[4] - signal type
	// Returns [value, hi32] where hi32 is 0 for float/double types.
	export function decodeSignal(
		data: Buffer,
		bitOffset: number,
		bitLength: number,
		endianess: boolean,
		signalType: SignalType
	): number[];

	// Encode signal according description
	// arg[0] - Data array
	// arg[1] - bitOffset
	// arg[2] - bitLength
	// arg[3] - endianess (true = Intel/LE, false = Motorola/BE)
	// arg[4] - signal type
	// arg[5] - value: low 32-bit word for integer types; full float/double for float types
	// arg[6] - (integer types only) high 32-bit word for 64-bit integers
	export function encodeSignal(
		data: Buffer,
		bitOffset: number,
		bitLength: number,
		endianess: boolean,
		signalType: SignalType,
		word1: number | boolean,
		word2?: number | boolean
	): void;
}
