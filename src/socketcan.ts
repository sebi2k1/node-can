/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

// -----------------------------------------------------------------------------
// CAN-Object

import * as can from "../build/Release/can.node";

// -----------------------------------------------------------------------------
/**
 * The Signals modules provides an interface to access the values/signals
 * encoded in CAN messages.
 * @module Signals
 */
import * as _signals from "../build/Release/can_signals.node";

import * as kcd from "./parse_kcd";

/**
 * @method createRawChannel
 * @param channel {string} Channel name (e.g. vcan0)
 * @param timestamps {bool} Whether or not timestamps shall be generated when reading a message
 * @param protocol {integer} optionally provide another default protocol value (default is CAN_RAW)
 * @return {RawChannel} a new channel object or exception
 * @for exports
 */
export function createRawChannel(
	channel: string,
	timestamps: boolean,
	protocol: number
): can.RawChannel {
	return new can.RawChannel(channel, timestamps, protocol, false);
}

interface ChannelOptions {
	timestamps?: boolean;
	protocol?: number;
	non_block_send?: boolean;
}

/**
 * @method createRawChannel
 * @param channel {string} Channel name (e.g. vcan0)
 * @param options {dict} list of options (timestamps, protocol, non_block_send)
 * @return {RawChannel} a new channel object or exception
 * @for exports
 */
function createRawChannelWithOptions(
	channel: string,
	options: ChannelOptions
): can.RawChannel {
	if (options === undefined) options = {};

	if (options.timestamps === undefined) options.timestamps = false;
	if (options.protocol === undefined) options.protocol = 1; /* CAN RAW */
	if (options.non_block_send === undefined) options.non_block_send = false;

	return new can.RawChannel(
		channel,
		options.timestamps,
		options.protocol,
		options.non_block_send
	);
}

/**
 * The actual signal.
 * @class Signal
 */
class Signal {
	readonly name: string;
	readonly spn: string;
	readonly bitOffset: number;
	readonly bitLength: number;
	readonly endianess: "little" | "big";
	readonly labels: Record<number, string>;
	readonly value?: kcd.Value;
	readonly muxGroup: number[];

	public currentValue?: number = undefined;

	public changeListeners: CallableFunction[] = [];
	public updateListeners: CallableFunction[] = [];

	constructor(desc: kcd.Signal) {
		/**
		 * Symbolic name
		 * @attribute name
		 * @final
		 */
		this.name = desc.name;
		this.spn = desc.spn;

		this.bitOffset = desc.bitOffset;
		this.bitLength = desc.bitLength;
		this.endianess = desc.endianess;

		this.value = desc.value;
		this.labels = desc.labels;

		/**
		 * this will allow triggering on mux'ed message ids.
		 */
		this.muxGroup = [desc.mux];
	}

	/**
	 * Keep track of listeners who want to be notified if this signal changes
	 * @method onChange
	 * @param listener JS callback to get notification
	 * @for Signal
	 */
	onChange(listener: CallableFunction) {
		this.changeListeners.push(listener);
		return listener;
	}

	/**
	 * Keep track of listeners who want to be notified if this signal updates
	 * @method onUpdate
	 * @param listener JS callback to get notification
	 * @for Signal
	 */
	onUpdate(listener: CallableFunction) {
		this.updateListeners.push(listener);
		return listener;
	}

	/**
	 * Remove listener from signal onChange and/or onUpdate
	 * @method removeListener
	 * @param listener to be removed
	 * @for Signal
	 */
	removeListener(listener: CallableFunction) {
		let idx = this.changeListeners.indexOf(listener);
		if (idx >= 0) this.changeListeners.splice(idx, 1);
		idx = this.updateListeners.indexOf(listener);
		if (idx >= 0) this.updateListeners.splice(idx, 1);
	}

	/**
	 * Set new value of this signal. Any local registered clients will
	 * receive a notification. Please note, no CAN message is actually
	 * send to the bus (@see DatabaseServer::send)
	 * @method update
	 * @param newValue {bool|double|integer} New value to set
	 * @for Signal
	 */
	update(newValue: number) {
		if (this.value) {
			// TODO: Move this block to a `Value.isValid(v)` function?
			if (this.value.maxValue && newValue > this.value.maxValue) {
				console.error(
					"ERROR : " +
						this.name +
						" value= " +
						newValue +
						" is outof bounds  > " +
						this.value.maxValue
				);
			}

			if (this.value.minValue && newValue < this.value.minValue) {
				console.error(
					"ERROR : " +
						this.name +
						" value= " +
						newValue +
						" is outof bounds  < " +
						this.value.minValue
				);
			}
		}

		const changed = this.currentValue !== newValue;
		this.currentValue = newValue;

		// Update all updateListeners, that the signal updated
		this.updateListeners.forEach((listener) => {
			listener(this);
		});

		// Nothing changed
		if (!changed) return;

		// Update all changelisteners, that the signal changed
		this.changeListeners.forEach((listener) => {
			listener(this);
		});
	}
}

// -----------------------------------------------------------------------------
/**
 * Just a container to keep the Signals.
 * @class Message
 */
class Message {
	readonly id: number;
	readonly name: string;
	readonly ext: boolean;
	readonly len: number;
	readonly interval: number;
	readonly muxed: boolean;
	readonly mux: kcd.Mux | undefined;
	readonly signals: Record<string, Signal> = {};

	constructor(msgDef: kcd.Message) {
		/**
		 * CAN identifier
		 * @attribute id
		 * @final
		 */
		this.id = msgDef.id;

		/**
		 * Extended Frame Format used
		 * @attribute ext
		 * @final
		 */
		this.ext = msgDef.ext;

		/**
		 * Symbolic name
		 * @attribute name
		 * @final
		 */
		this.name = msgDef.name;

		/**
		 * Length in bytes of resulting CAN message
		 *
		 * @attribute len
		 * @final
		 */
		this.len = msgDef.length;

		/**
		 * This is the time frame that the message gets generated
		 *
		 * @attribute interval
		 * @final
		 */
		this.interval = msgDef.interval;

		/**
		 * This tells us the message is mutliplexed.
		 *
		 * @attribute muxed
		 * @final
		 */
		this.muxed = msgDef.muxed;

		/**
		 * Multiplexor parameter (just one supported right now).
		 *
		 * @attribute mux
		 * @final
		 */
		this.mux = msgDef.mux;

		/**
		 * Named information to inform that the frame is CAN_FD format .
		 * @attribute Boolean
		 * @final
		 */
		// this.canfd = msgDef.canfd;

		msgDef.signals.forEach((s) => {
			if (this.signals[s.name] && this.signals[s.name].muxGroup) {
				this.signals[s.name].muxGroup.push(s.mux);
			} else {
				this.signals[s.name] = new Signal(s);
			}
		});
	}
}

// -----------------------------------------------------------------------------
/**
 * A DatabaseService is usually generated once per bus to collect signals
 * coded in the CAN messages according a DB description.
 * @class DatabaseService
 * @constructor DatabaseService
 * @param channel RAW channel
 * @param db_desc Set of rules to decode/encode signals (@parse_kcd.js)
 * @return a new DatabaseService
 * @for DatabaseService
 */
class DatabaseService {
	readonly messages: Record<string, Message> = {};
	constructor(private channel: can.RawChannel, busDef: kcd.Bus) {
		busDef.messages.forEach((m) => {
			const id = m.id | ((m.ext ? 1 : 0) << 31);

			const nm = new Message(m);
			this.messages[id] = nm;
			this.messages[m.name] = nm;
		});

		// Subscribe to any incoming messages
		channel.addListener("onMessage", this.onMessage, this);
	}

	// Callback for incoming messages
	onMessage(msg: can.Message) {
		if (msg == undefined) return;

		// RTR (Remote-Transmit-Request) dont have payload
		if (msg.rtr) return;

		const id = msg.id | ((msg.ext ? 1 : 0) << 31);

		const m = this.messages[id];

		if (!m) {
			return;
		}

		let mux_count = -1;

		if (m.muxed && m.mux) {
			const b_mux = _signals.decodeSignal(
				msg.data,
				m.mux.offset,
				m.mux.length,
				true,
				false
			);
			mux_count = b_mux[0] + (b_mux[1] << 32);
		}

		// Let the C-Portition extract and convert the signal
		for (const i in m.signals) {
			const s = m.signals[i];

			if (s.value === undefined) continue;

			// if this is a mux signal and the muxor isnt in my list...
			if (m.muxed && s.muxGroup.indexOf(mux_count) == -1) {
				continue;
			}

			const ret = _signals.decodeSignal(
				msg.data,
				s.bitOffset,
				s.bitLength,
				s.endianess == "little",
				s.value.type == "signed"
			);

			let val = ret[0] + (ret[1] << 32);

			if (s.value.slope) val *= s.value.slope;

			if (s.value.intercept) val += s.value.intercept;

			s.update(val);
		}
	}

	/**
	 * Construct a CAN message and encode all related signals according
	 * the rules. Finally send the message to the bus.
	 * @method send
	 * @param msg_name Name of the message to generate (indicate mux by append .MUX_VALUE in hex)
	 * @for DatabaseService
	 */
	send(msg_name: string) {
		const args = msg_name.split("."); // allow for mux'ed messages sent.

		const m = this.messages[args[0]];
		const mux = args.length > 1 ? args[1] : undefined;

		if (!m) throw msg_name + " not defined";

		const canmsg = {
			id: m.id,
			ext: m.ext,
			rtr: false,
			// for CANFD data buffer 64 bytes
			data: m.len > 0 && m.len < 64 ? Buffer.alloc(m.len) : Buffer.alloc(64),
		};

		canmsg.data.fill(0); // should be 0xFF for j1939 message def.

		if (mux && m.mux)
			_signals.encodeSignal(
				canmsg.data,
				m.mux.offset,
				m.mux.length,
				true,
				false,
				parseInt(mux, 16)
			);

		Object.values(m.signals).forEach((s) => {
			if (s.value == undefined) return;

			if (mux) {
				if (s.muxGroup.indexOf(parseInt(mux, 16)) === -1) {
					return;
				}
			}

			let val = s.currentValue!;

			// Apply factor/intercept and convert to Integer
			if (s.value) {
				val -= s.value.intercept;
				val /= s.value.slope;
			}

			// Make sure we are sending an integer because the division above could change it to a float.
			val = Math.round(val);

			if (m.len == 0) {
				return;
			}

			const word1 = val & 0xffffffff;
			let word2 = 0;

			// shift doesn't work above 32 bit, only do this if required to save cycles
			if (val > 0xffffffff) {
				word2 = val / Math.pow(2, 32);
			}

			_signals.encodeSignal(
				canmsg.data,
				s.bitOffset,
				s.bitLength,
				s.endianess == "little",
				s.value!.type == "signed",
				word1,
				word2
			);
		});

		this.channel.send(canmsg);
	}
}

/**
 * @method parseNetworkDescription
 * @param file {string} Path to KCD file to parse
 * @return DB description to be used in DatabaseService
 * @for exports
 */
exports.parseNetworkDescription = kcd.parseKcdFile;
exports.DatabaseService = DatabaseService;
