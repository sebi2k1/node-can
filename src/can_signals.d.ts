declare module "can_signals" {
    // Decode signal according description
    // arg[0] - Data array
    // arg[1] - offset zero indexed
    // arg[2] - bitLength one indexed
    // arg[3] - endianess
    // arg[4] - signed flag
    function decodeSignal(
        data: Buffer,
        bitOffset: number,
        bitLength: number,
        endianess: boolean,
        signed: boolean,
    ): number[];

    // Encode signal according description
    // arg[0] - Data array
    // arg[1] - bitOffset
    // arg[2] - bitLength
    // arg[3] - endianess
    // arg[4] - signed flag
    // arg[5] - first 4 bytes value to encode
    // arg[6] - second 4 bytes value to encode
    function encodeSignal(
        data: Buffer,
        bitOffset: number,
        bitLength: number,
        endianess: boolean,
        signed: boolean,
        word1: number | boolean,
        word2?: number | boolean,
    ): void;
}