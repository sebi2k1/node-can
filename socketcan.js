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

//-----------------------------------------------------------------------------
// CAN-Object

var can = require('./build/Release/can');
var buffer = require('buffer');

/**
 * @method createRawChannel
 * @param channel {string} Channel name (e.g. vcan0)
 * @return {RawChannel} a new channel object or exception
 * @for exports
 */
exports.createRawChannel = function(channel, timestamps) { return new can.RawChannel(channel, timestamps); }

//-----------------------------------------------------------------------------
/**
 * The Signals modules provides an interface to access the values/signals
 * encoded in CAN messages.
 * @module Signals
 */

var _signals = require('./build/Release/can_signals');

var kcd = require('./parse_kcd');

/**
 * The actual signal.
 * @class Signal
 */
function Signal(desc)
{
        /**
         * Symbolic name
         * @attribute name
         * @final
         */
	this.name = desc['name'];
	this.spn = desc['spn'];

	this.bitOffset = desc['bitOffset'];
	this.bitLength = desc['bitLength'];
	this.endianess = desc['endianess'];
	this.type = desc['type'];

	this.intercept = desc['intercept'];
	this.slope = desc['slope'];

	this.minValue = desc['minValue'];
	this.maxValue = desc['maxValue'];

	this.unit = desc['unit'];

	/**
	 * Label set for defined states of the signal.
	 */
	this.labels = desc['labels'];

	/**
	 * this will allow triggering on mux'ed message ids.
	 */
	this.muxGroup = [ desc['mux'] ];

	/**
	 * Current value
	 *
	 * @attribute value
	 * @final
	 */
	this.value = null;

	this.changelisteners = [];
	this.updateListeners = [];
}

/**
 * Keep track of listeners who want to be notified if this signal changes
 * @method onChange
 * @param listener JS callback to get notification
 * @for Signal
 */
Signal.prototype.onChange = function(listener) {
	this.changelisteners.push(listener);
	return listener;
}

/**
 * Keep track of listeners who want to be notified if this signal updates
 * @method onUpdate
 * @param listener JS callback to get notification
 * @for Signal
 */
Signal.prototype.onUpdate = function(listener) {
	this.updateListeners.push(listener);
	return listener;
}

/**
 * Remove listener from signal onChange and/or onUpdate
 * @method removeListener
 * @param listener to be removed
 * @for Signal
 */
Signal.prototype.removeListener = function(listener) {
	var idx = this.changelisteners.indexOf(listener);
	if (idx >= 0) this.changelisteners.splice(idx, 1);
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
Signal.prototype.update = function(newValue) {

	if (newValue > this.maxValue) {
		console.error("ERROR : " + this.name + " value= " + newValue
				+ " is outof bounds  > " + this.maxValue);
	} else if (newValue < this.minValue) {
		console.error("ERROR : " + this.name + " value= " + newValue
				+ " is outof bounds  < " + this.minValue);
	}
	var changed = this.value !== newValue;
	this.value = newValue;
	
	// Update all updateListeners, that the signal updated
	for (f in this.updateListeners) {
		this.updateListeners[f](this);
	}
	// Nothing changed
	if ( ! changed) return;

	// Update all changelisteners, that the signal changed
	for (f in this.changelisteners) {
		this.changelisteners[f](this);
	}
}

//-----------------------------------------------------------------------------
/**
 * Just a container to keep the Signals.
 * @class Message
 */
function Message(desc)
{
        /**
         * CAN identifier
         * @attribute id
         * @final
         */
	this.id = desc.id;

	/**
         * Extended Frame Format used
         * @attribute ext
         * @final
         */
	this.ext = desc.ext;

        /**
         * Symbolic name
         * @attribute name
         * @final
         */
	this.name = desc.name;

        /**
         * Length in bytes of resulting CAN message
	 *
	 * @attribute len
	 * @final
	 */
	this.len = desc.length;

	/**
	 * This is the time frame that the message gets generated
	 *
	 * @attribute interval
	 * @final
	 */
	this.interval = desc.interval;

	/**
	 * This is tells us the message is mutliplexed.
	 *
	 * @attribute muxed
	 * @final
	 */
	this.muxed = desc.muxed;

	/**
         * Named array of signals within this message. Accessible via index and name.
         * @attribute {Signal} signals
         * @final
         */
	this.signals = [];

	for (i in desc['signals']) {
		var s = desc['signals'][i];
		if (this.signals[s.name] && this.signals[s.name].muxGroup) {
			this.signals[s.name].muxGroup.push(s.mux);
		} else {
			this.signals[s.name] = new Signal(s);
		}
	}
}

//-----------------------------------------------------------------------------
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
function DatabaseService(channel, db_desc) {
	this.channel = channel;

        /**
         * Named array of known messages. Accessible via index and name.
         * @attribute {Message} messages
         */
	this.messages = [];

	for (i in db_desc['messages']) {
		var m = db_desc['messages'][i];
		var id = m.id | (m.ext ? 1 : 0) << 31;

		var nm = new Message(m);
		this.messages[id] = nm;
		this.messages[m.name] = nm;
	}

	// Subscribe to any incoming messages
	channel.addListener("onMessage", this.onMessage, this);
}

// Callback for incoming messages
DatabaseService.prototype.onMessage = function (msg) {
	if (msg == undefined)
		return;
	if (msg.rtr)
		return;

	id = msg.id | (msg.ext ? 1 : 0) << 31;

	var m = this.messages[id];

	if (!m)
	{
		return;
	}

	// this is the possible multiplexor for the signals coming in.
	var b1mux = _signals.decode_signal(msg.data, 0, 8, true, false);

	// Let the C-Portition extract and convert the signal
	for (i in m.signals) {
		var s = m.signals[i];

		if (s.value === undefined)
			continue;

		// if this is a mux signal and the muxor isnt in my list...
		if (m.muxed && s.muxGroup && s.muxGroup.indexOf(b1mux) == -1) {
			continue;
		}

		var val = _signals.decode_signal(msg.data, s.bitOffset, s.bitLength,
				s.endianess == 'little', s.type == 'signed');

		if (s.slope)
			val *= s.slope;

		if (s.intercept)
			val += s.intercept;

		s.update(val);
	}
}

/**
 * Construct a CAN message and encode all related signals according
 * the rules. Finally send the message to the bus.
 * @method send
 * @param msg_name Name of the message to generate
 * @for DatabaseService
 */
DatabaseService.prototype.send = function (msg_name) {
	var args = msg_name.split("."); // allow for mux'ed messages sent.

	var m = this.messages[args[0]];
	var mux = (args.length > 1) ? args[1] : undefined;

	if (!m)
		throw msg_name + " not defined";

	var canmsg = {
		id: m.id,
		ext: m.ext,
		rtr: false,
		data : (m.len > 0 && m.len < 8) ? new Buffer(m.len) : new Buffer(8)
	};

	canmsg.data.fill(0); // should be 0xFF for j1939 message def.


	if (mux) {
		_signals.encode_signal(canmsg.data, 0, 8, true, false,
				parseInt(mux, 16));
	}
	for (i in m.signals) {
		var s = m.signals[i];
		if (s.value == undefined)
			continue;

		if (mux) {
			if (s.muxGroup.indexOf(parseInt(mux, 16)) === -1) {
				continue;
			}
		}

		var val = s.value;

		// Apply factor/intercept and convert to Integer
		if (s.intercept)
			val -= s.intercept;

		if (s.slope)
			val /= s.slope;

		if (typeof(val) == 'double')
			val = parseInt(Math.round(val));

		if (m.len == 0) {
			return;
		}

		_signals.encode_signal(canmsg.data, s.bitOffset, s.bitLength,
				s.endianess == 'little', s.type == 'signed', val);
	}

	this.channel.send(canmsg);
}

/**
 * @method parseNetworkDescription
 * @param file {string} Path to KCD file to parse
 * @return DB description to be used in DatabaseService
 * @for exports
 */
exports.parseNetworkDescription = kcd.parseKcdFile;
exports.DatabaseService = DatabaseService;
