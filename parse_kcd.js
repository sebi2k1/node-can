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

var fs = require('fs');
var xml2js = require('xml2js');

//-----------------------------------------------------------------------------

exports.parseKcdFile = function(file) {
	var result = {}; // Result will be a dictionary describing the whole network

	var data = fs.readFileSync(file)
	
	var parser = new xml2js.Parser({explicitArray: true});
		
	parser.parseString(data, function(e, i) {
		result.nodes = {};

		var d = i['NetworkDefinition'];

		for (n in d['Node']) {
			var node = d['Node'][n]['$'];
			
			result.nodes[node['id']] = {};
			result.nodes[node['id']].name = node['name'];
			result.nodes[node['id']].buses = {};
		}
		
		result.buses = {};
		for (b in d['Bus']) {
			var bus = d['Bus'][b]['$'];
			
			result.buses[bus['name']] = {};
			var new_bus = result.buses[bus['name']];

			new_bus['messages'] = [];
			for (m in d['Bus'][b]['Message']) {
				var message = d['Bus'][b]['Message'][m]['$'];
				var producers = d['Bus'][b]['Message'][m]['Producer'];
				var consumers = d['Bus'][b]['Message'][m]['Consumer'];
				
				var _m = {
					name: message.name,
					id: parseInt(message.id, 16),
					ext: message.format == 'extended',
					triggered: message.triggered == 'true',
					length: message.length,
					interval: parseInt(message.interval)
				};

				for (p in producers) {
					for (n in producers[p]['NodeRef']) {
						var id = producers[p]['NodeRef'][n]['$']['id'];
						
						if (result.nodes[id])
						{
							if (result.nodes[id].buses[bus['name']] == undefined)
								result.nodes[id].buses[bus['name']] = { produces: [], consume: []}
							
							result.nodes[id].buses[bus['name']].produces.push(_m);
						}
					}
				}
				
				for (c in consumers) {
					for (n in consumers[c]['NodeRef']) {
						var id = consumers[c]['NodeRef'][n]['$']['id'];
						
						if (result.nodes[id])
						{
							if (result.nodes[id].buses[bus['name']] == undefined)
								result.nodes[id].buses[bus['name']] = { produces: [], consume: []}
							
							result.nodes[id].buses[bus['name']].consumes.push(_m);
						}
					}
				}
				
				if (!_m.interval)
					_m.interval = 0;
				
				new_bus['messages'].push(_m);
	
				_m.signals = [];
				
				var maxOffset = 0;
				
				for (s in d['Bus'][b]['Message'][m]['Signal']) {
					var signal = d['Bus'][b]['Message'][m]['Signal'][s]['$'];
					var value = d['Bus'][b]['Message'][m]['Signal'][s]['Value'];
					
					var _s = {
						name: signal.name,
						bitLength: signal.length ? parseInt(signal.length) : 1,
						endianess: signal.endianess ? signal.endianess : 'little',
					};

					if (Array.isArray(value)) {
						_s.scale = value[0]['$'].slope ? parseFloat(value[0]['$'].slope) : 1.0;
						_s.offset = value[0]['$'].intercept ? parseFloat(value[0]['$'].intercept) : 0.0;
						_s.unit = value[0]['$'].unit ? value[0]['$'].unit : "";
						_s.minValue = value[0]['$'].min ? value[0]['$'].min : undefined;
						_s.maxValue = value[0]['$'].max ? value[0]['$'].max : undefined;
					}
					
					var offset_num = parseInt(signal.offset) + _s.bitLength;

					if (offset_num > maxOffset)
						maxOffset = offset_num;

					_s.bitOffset = parseInt(signal.offset);

					_m.signals.push(_s);
				}
				
				if (!_m.length) {
					_m.length = parseInt(maxOffset / 8);
					if (maxOffset % 8 > 0)
						_m.length++;
				}
			}
		}
	});

	// NOTE: Not sure if it is safe here to access result, but I guess parsing the XML file is more or less synchronous.
	
	return result;
}
