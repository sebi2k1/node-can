/* Copyright Sebastian Haas <sebastian$sebastianhaas.info>. All rights reserved.
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

import * as fs from "fs";
import * as xml2js from "xml2js";

export class J1939 {
	constructor(
		public AAC: number,
		public Function: number,
		public Vehicle: number,
		public Identity: number,
		public Industry: number,
		public System: number,
		public Manufacture: number
	) {}

	getName() {
		const name = Buffer.alloc(8);
		name[7] =
			((this.AAC & 0x1) << 7) |
			((this.Industry & 0x7) << 4) |
			(this.Vehicle & 0xf);
		name[6] = (this.System << 1) & 0xfe;
		name[5] = this.Function & 0xff;
		name[4] = 0; // function Instance & ECU instance
		name[3] = (this.Manufacture >> 3) & 0xff;
		name[2] = ((this.Manufacture & 0x7) << 5) | ((this.Identity >> 16) & 0x1f);
		name[1] = (this.Identity >> 8) & 0xff;
		name[0] = this.Identity & 0xff;

		return name;
	}
}

interface SignalRef {
	id: number;
	signal_name: string;
}

export class BusRefs {
	public produces: number[] = [];
	public consumes: SignalRef[] = [];
}

export class Node {
	public buses: Record<string, BusRefs> = {};

	constructor(
		public id: number,
		public name: string,
		public device: object,
		public j1939: J1939
	) {}
}

export class Signal {
	constructor(
		public name: string,
		public spn: string,
		public bitOffset: number,
		public bitLength: number,
		public endianess: "little" | "big",
		public labels: Record<number, string>,
		public mux: number,
		public slope: number = 1.0,
		public intercept: number = 0.0,
		public unit: string = "",
		public type: "signed" | "unsigned" = "unsigned",
		public defaultValue: number = 0.0,
		public minValue?: number,
		public maxValue?: number
	) {}
}

export class NodeRef {
	constructor(public id: number) {}
}

export class Mux {
	constructor(
		public name: string,
		public offset: number,
		public length: number
	) {}
}

export class Message {
	public signals: Signal[] = [];
	public producers: NodeRef[] = [];

	constructor(
		public name: string,
		public id: number,
		public ext: boolean,
		public triggered: boolean,
		public length: number,
		public interval: number,
		public muxed: boolean,
		public mux?: Mux
	) {}
}

export class Bus {
	public messages: Message[] = [];
}

export class CanNetwork {
	public nodes: Record<string, Node> = {};
	public buses: Record<string, Bus> = {};
}

interface XmlSignalDef {
	name: string;
	spn: string;
	length: string;
	offset: string;
	endianess: "big" | "little";
}

// -----------------------------------------------------------------------------
function makeSignalFromXml(
	xmlSignal: XmlSignalDef,
	xmlValue: Record<string, unknown>,
	labelset: Record<string, unknown>,
	muxCount: number
): Signal {
	const bitOffset = parseInt(xmlSignal.offset);

	const labelSet: Record<number, string> = {};
	// add label sets from the database.
	if (Array.isArray(labelset)) {
		const rawLabels = labelset[0]["Label"];
		if (rawLabels != undefined) {
			rawLabels.forEach((l: any) => {
				labelSet[l["$"].value] = l["$"].name;
			});
		}
	}

	// add Values from the database
	const rawValue = Array.isArray(xmlValue) ? xmlValue[0]["$"] : undefined;

	const newSignal = new Signal(
		xmlSignal.name,
		xmlSignal.spn,
		bitOffset,
		parseInt(xmlSignal.length ?? 1),
		xmlSignal?.endianess ?? "little",
		labelSet,
		muxCount,
		parseFloat(rawValue?.slope ?? 1.0),
		parseFloat(rawValue?.intercept ?? 0.0),
		rawValue?.unit ?? "",
		rawValue?.type ?? "unsigned",
		parseFloat(rawValue?.defaultValue ?? 0.0),
		rawValue?.min ?? undefined,
		rawValue?.max ?? undefined
	);

	return newSignal;
}

export function parseKcdFile(file: fs.PathOrFileDescriptor) {
	// Result will be a dictionary describing the whole network
	const network = new CanNetwork();

	const data = fs.readFileSync(file);

	const parser = new xml2js.Parser({ explicitArray: true });

	parser.parseString(data, function (e, parsed) {
		const networkDefinition = parsed["NetworkDefinition"];

		for (const n in networkDefinition["Node"]) {
			const rawNode = networkDefinition["Node"][n]["$"];

			const newNode = new Node(
				rawNode["id"],
				rawNode["name"],
				rawNode["device"],
				new J1939(
					rawNode["J1939AAC"],
					rawNode["J1939Function"],
					rawNode["J1939Vehicle"],
					rawNode["J1939IdentityNumber"],
					rawNode["J1939IndustryGroup"],
					rawNode["J1939System"],
					rawNode["J1939ManufacturerCode"]
				)
			);

			network.nodes[rawNode["id"]] = newNode;
		}

		for (const b in networkDefinition["Bus"]) {
			const rawBus = networkDefinition["Bus"][b]["$"];

			const busName: string = rawBus["name"];
			const newBus = new Bus();

			const rawBusMessages = networkDefinition["Bus"][b]["Message"];
			for (const messageKey in rawBusMessages) {
				const rawMessage = rawBusMessages[messageKey];
				const message = rawMessage["$"];
				const producers = rawMessage["Producer"];
				const multiplex = rawMessage["Multiplex"];

				const newMessage = new Message(
					message.name,
					parseInt(message.id, 16),
					message.format == "extended",
					message.triggered == "true",
					message.length ? parseInt(message.length) : 0,
					message.interval ? parseInt(message.interval) : 0,
					multiplex != undefined
				);

				// Add messages going out and from whom.
				for (const p in producers) {
					const nodeRefs = producers[p]["NodeRef"];
					for (const n in nodeRefs) {
						const nodeRefId: number = nodeRefs[n]["$"]["id"];

						newMessage.producers.push(new NodeRef(nodeRefId));

						// Look up the node by _id_ (number), not name.
						const nodeDef: Node = network.nodes[nodeRefId];

						if (nodeDef) {
							if (nodeDef.buses[busName] == undefined) {
								nodeDef.buses[busName] = new BusRefs();
							}
							nodeDef.buses[busName].produces.push(newMessage.id);
						}
					}
				}

				if (!newMessage.interval) newMessage.interval = 0;

				let maxOffset = 0;

				// look for multiplexed messages
				for (const mux in multiplex) {
					newMessage.mux = {
						name: multiplex[mux]["$"]["name"],
						offset: parseInt(multiplex[mux]["$"]["offset"]),
						length: parseInt(multiplex[mux]["$"]["length"]),
					};

					for (const mg in multiplex[mux]["MuxGroup"]) {
						const muxmsg = multiplex[mux]["MuxGroup"][mg]["$"];

						for (const s in multiplex[mux]["MuxGroup"][mg]["Signal"]) {
							const signal = multiplex[mux]["MuxGroup"][mg]["Signal"][s]["$"];
							const value =
								multiplex[mux]["MuxGroup"][mg]["Signal"][s]["Value"];
							const labelset =
								multiplex[mux]["MuxGroup"][mg]["Signal"][s]["LabelSet"];

							// Added multiplexor
							const muxCount = parseInt(muxmsg["count"]);

							const newSignal = makeSignalFromXml(
								signal,
								value,
								labelset,
								muxCount
							);

							newMessage.signals.push(newSignal);

							const offset_num = newSignal.bitOffset + newSignal.bitLength;

							if (offset_num > maxOffset) maxOffset = offset_num;
						}
					}

					// only one muxer supported right now
					break;
				}

				const signals = networkDefinition["Bus"][b]["Message"][messageKey]["Signal"]
				for (const s in signals) {
					const signal = signals[s]["$"];
					const value = signals[s]["Value"];
					const labelset = signals[s]["LabelSet"];
					const consumers = signals[s]["Consumer"];

					const newSignal = makeSignalFromXml(signal, value, labelset, 0);

					// Add listeners / targets for the message.
					for (const c in consumers) {
						for (const n in consumers[c]["NodeRef"]) {
							const id = consumers[c]["NodeRef"][n]["$"]["id"];

							if (network.nodes[id]) {
								if (network.nodes[id].buses[busName] == undefined)
									network.nodes[id].buses[busName] = {
										produces: [],
										consumes: [],
									};

								network.nodes[id].buses[busName].consumes.push({
									id: newMessage.id,
									signal_name: newSignal.name,
								});
							}
						}
					}

					newMessage.signals.push(newSignal);

					const offset_num = newSignal.bitOffset + newSignal.bitLength;

					if (offset_num > maxOffset) maxOffset = offset_num;
				}

				// Calculate length based on define signals
				if (!newMessage.length) {
					newMessage.length = maxOffset / 8;

					if (maxOffset % 8 > 0) newMessage.length++;
				}

				newBus.messages.push(newMessage);
			}

			network.buses[busName] = newBus;
		}
	});

	// NOTE: Not sure if it is safe here to access result, but I guess parsing the XML file is more or less synchronous.

	return network;
}
