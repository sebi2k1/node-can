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

import * as fs from 'fs';
import * as xml2js from 'xml2js';

export class J1939 {
    constructor(
        public AAC: number,
        public Function: number,
        public Vehicle: number,
        public Identity: number,
        public Industry: number,
        public System: number,
        public Manufacture: number,
    ) {}

    getName() {
        var name = Buffer.alloc(8);
        name[7] = ((this.AAC & 0x1) << 7) | ((this.Industry & 0x7) << 4) | (this.Vehicle & 0xF);
        name[6] = (this.System) << 1  & 0xFE;
        name[5] = this.Function & 0xFF;
        name[4] = 0; // function Instance & ECU instance
        name[3] = (this.Manufacture >> 3) & 0xFF ;
        name[2] = ((this.Manufacture & 0x7) << 5) | ( (this.Identity >> 16) & 0x1F );
        name[1] = (this.Identity >> 8 ) & 0xFF;
        name[0] = this.Identity & 0xFF;

        return name;
    }
}

export class Node {
    constructor(
        public name: string,
        public buses: object,
        public device: object,
        public j1939: J1939,
    ) {}
}

export class Value {
    constructor(
        public slope: number = 1.0,
        public intercept: number = 0.0,
        public unit: string = "",
        public type: 'signed' | 'unsigned' = 'unsigned',
        public defaultValue: number = 0.0,
        public minValue?: number,
        public maxValue?: number,
    ) {}
}

export class Signal {
    constructor(
        public name: string,
        public bitLength: number,
        public endianess: 'little' | 'big',
        public spn: string,
        public bitOffset: number,
        public labels: Record<number, string>,
        public value?: Value,
    ) {}
}

export class Message {
    public signals: Signal[] = [];

    constructor(
        public name: string,
        public id: number,
        public ext: boolean,
        public triggered: boolean,
        public length: number,
        public interval: number,
        public muxed: boolean,
        public mux: undefined
    ) {}
}

export class Bus {
    public messages: Message[] = [];

    constructor() {}

}

export class CanNetwork {
    public nodes: Record<string, Node> = {};
    public buses: Record<string, Bus> = {};
}

//-----------------------------------------------------------------------------
function makeSignalFromXml(xmlSignal: Record<string, any>, xmlValue: any, labelset: any): Signal {
    const bitOffset = parseInt(xmlSignal.offset);

    const labelSet: Record<number, string> = {};
    // add label sets from the database.
    if (Array.isArray(labelset)) {
        var rawLabels = labelset[0]['Label'];
        if (rawLabels != undefined) {
            rawLabels.forEach((l: any) => {
                labelSet[l['$'].value] = l['$'].name;
            });
        }
    }

    let value: Value | undefined;
    // add Values from the database
    if (Array.isArray(xmlValue)) {
        const rawValue = xmlValue[0]['$'];

        value = new Value(
            rawValue.slope ? parseFloat(rawValue.slope) : 1.0,
            rawValue.intercept ? parseFloat(rawValue.intercept) : 0.0,
            rawValue.unit ? rawValue.unit : "",
            rawValue.type ? rawValue.type : "unsigned",
            rawValue.defaultValue ? parseFloat(rawValue.defaultValue) : 0.0,
            rawValue.min ? rawValue.min : undefined,
            rawValue.max ? rawValue.max : undefined,
        );
    }

    const newSignal = new Signal(
        xmlSignal.name,
        xmlSignal.length ? parseInt(xmlSignal.length) : 1,
        xmlSignal.endianess ? xmlSignal.endianess : 'little',
        xmlSignal.spn,
        bitOffset,
        labelSet,
        value,
    );

    return newSignal
}

export function parseKcdFile(file: fs.PathOrFileDescriptor) {
    // Result will be a dictionary describing the whole network
    const network = new CanNetwork();

    const data = fs.readFileSync(file);

    const parser = new xml2js.Parser({explicitArray: true});

    parser.parseString(data, function(e, parsed) {
        const networkDefinition = parsed['NetworkDefinition'];

        for (const n in networkDefinition['Node']) {
            const rawNode = networkDefinition['Node'][n]['$'];

            const newNode = new Node(
                rawNode['name'],
                {},
                rawNode['device'],
                new J1939(
                    rawNode['J1939AAC'],
                    rawNode['J1939Function'],
                    rawNode['J1939Vehicle'],
                    rawNode['J1939IdentityNumber'],
                    rawNode['J1939IndustryGroup'],
                    rawNode['J1939System'],
                    rawNode['J1939ManufacturerCode'],
                )
            )

            network.nodes[rawNode['id']] = newNode;
        }

        for (const b in networkDefinition['Bus']) {
            var rawBus = networkDefinition['Bus'][b]['$'];

            const newBus = new Bus();

            const rawBusMessages = networkDefinition['Bus'][b]['Message'];
            for (const messageKey in rawBusMessages) {
                const rawMessage = rawBusMessages[messageKey];
                const message = rawMessage['$'];
                const producers = rawMessage['Producer'];
                const multiplex = rawMessage['Multiplex'];

                var newMessage = new Message(
                    message.name,
                    parseInt(message.id, 16),
                    message.format == 'extended',
                    message.triggered == 'true',
                    message.length ? parseInt(message.length) : 0,
                    message.interval ? parseInt(message.interval) : 0,
                    (multiplex != undefined),
                    undefined
                )

                // Add messages going out and from whom.
                for (const p in producers) {
                    for (const n in producers[p]['NodeRef']) {
                        var id = producers[p]['NodeRef'][n]['$']['id'];

                        if (network.nodes[id])
                        {
                            if (network.nodes[id].buses[rawBus['name']] == undefined)
                                network.nodes[id].buses[rawBus['name']] = { produces: [], consumes: []}

                            network.nodes[id].buses[rawBus['name']].produces.push(newMessage.id);
                        }
                    }
                }

                if (!newMessage.interval)
                    newMessage.interval = 0;

                var maxOffset = 0;

                // look for multiplexed messages
                for (const mux in multiplex) {
                    newMessage.mux = {
                        name: multiplex[mux]['$']['name'],
                        offset: parseInt(multiplex[mux]['$']['offset']),
                        length: parseInt(multiplex[mux]['$']['length'])
                    }

                    for (const mg in multiplex[mux]['MuxGroup']) {
                        var muxmsg = multiplex[mux]['MuxGroup'][mg]['$'];

                        for (const s in multiplex[mux]['MuxGroup'][mg]['Signal']) {
                            var signal = multiplex[mux]['MuxGroup'][mg]['Signal'][s]['$'];
                            var value = multiplex[mux]['MuxGroup'][mg]['Signal'][s]['Value'];
                            var labelset = multiplex[mux]['MuxGroup'][mg]['Signal'][s]['LabelSet'];

                            var newSignal = makeSignalFromXml(signal, value, labelset)

                            // Added multiplexor
                            newSignal.mux = parseInt(muxmsg['count'])

                            newMessage.signals.push(newSignal);

                            var offset_num = newSignal.bitOffset + newSignal.bitLength;

                            if (offset_num > maxOffset)
                                maxOffset = offset_num;
                        }
                    }

                    // only one muxer supported right now
                    break
                }

                for (const s in networkDefinition['Bus'][b]['Message'][messageKey]['Signal']) {
                    var signal = networkDefinition['Bus'][b]['Message'][messageKey]['Signal'][s]['$'];
                    var value = networkDefinition['Bus'][b]['Message'][messageKey]['Signal'][s]['Value'];
                    var labelset = networkDefinition['Bus'][b]['Message'][messageKey]['Signal'][s]['LabelSet'];
                    var consumers = networkDefinition['Bus'][b]['Message'][messageKey]['Signal'][s]['Consumer'];

                    var newSignal = makeSignalFromXml(signal, value, labelset)

                    // Add listeners / targets for the message.
                    for (const c in consumers) {
                        for (const n in consumers[c]['NodeRef']) {
                            var id = consumers[c]['NodeRef'][n]['$']['id'];

                            if (network.nodes[id])
                            {
                                if (network.nodes[id].buses[rawBus['name']] == undefined)
                                    network.nodes[id].buses[rawBus['name']] = { produces: [], consumes: []}

                                network.nodes[id].buses[rawBus['name']].consumes.push({ id: newMessage.id, signal_name: newSignal.name });
                            }
                        }
                    }

                    newMessage.signals.push(newSignal);

                    var offset_num = newSignal.bitOffset + newSignal.bitLength;

                    if (offset_num > maxOffset)
                        maxOffset = offset_num;
                }

                // Calculate length based on define signals
                if (!newMessage.length) {
                    newMessage.length = maxOffset / 8;

                    if (maxOffset % 8 > 0)
                        newMessage.length++;
                }

                newBus.messages.push(newMessage);
            }

            network.buses[rawBus['name']] = newBus;
        }
    });

    // NOTE: Not sure if it is safe here to access result, but I guess parsing the XML file is more or less synchronous.

    return network;
}
