export interface Message {
	id: number;
	ext: boolean;
	rtr: boolean;
	data: Buffer;
}

export interface RawChannel {
	/**
	 * Add listener to receive certain notifications
	 * @method addListener
	 * @param event {string} onMessage to register for incoming messages
	 * @param callback {any} JS callback object
	 * @param instance {any} Optional instance pointer to call callback
	 */
	addListener(
		event: string,
		callback: CallableFunction,
		instance?: object
	): void;

	/**
	 * Start operation on this CAN channel
	 * @method start
	 */
	start(): void;

	/**
	 * Stop any operations on this CAN channel
	 * @method stop
	 */
	stop(): void;

	/**
	 * Send a CAN message immediately.
	 *
	 * PLEASE NOTE: By default, this function may block if the Tx buffer is not available. Please use
	 * createRawChannelWithOptions({non_block_send: false}) to get non-blocking sending activated.
	 *
	 * @method send
	 * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext or rtr
	 */
	send(message: Message): void;

	/**
	 * Send a CAN FD message immediately.
	 *
	 * PLEASE NOTE: By default, this function may block if the Tx buffer is not available. Please use
	 * createRawChannelWithOptions({non_block_send: false}) to get non-blocking sending activated.
	 *
	 * PLEASE NOTE: Might fail if underlying device doesnt support CAN FD. Structure is not yet validated.
	 *
	 * @method sendFD
	 * @param message {Object} JSON object describing the CAN message, keys are id, length, data {Buffer}, ext
	 */
	sendFD(message: Message): void;

	/**
	 * Set a list of active filters to be applied for incoming messages
	 * @method setRxFilters
	 * @param filters {Object} single filter or array of filter e.g. { id: 0x1ff, mask: 0x1ff, invert: false}, result of (id & mask)
	 */
	setRxFilters(filters: Record<string, unknown>[]): void;

	/**
	 * Set a list of active filters to be applied for errors
	 * @method setErrorFilters
	 * @param errorMask {Uint32} CAN error mask
	 */
	setErrorFilters(errorMask: number): void;

	/**
	 * Disable loopback of channel. By default it is activated
	 * @method disableLoopback
	 */
	disableLoopback(): void;
}

const can = require("../build/Release/can");

export const RawChannel: {
	new (
		name: string,
		timestamps?: boolean,
		protocol?: number,
		non_block_send?: boolean
	): RawChannel;
} = can.RawChannel;
