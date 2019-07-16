/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define __STDC_LIMIT_MACROS

#include <nan.h>
#include <node_buffer.h>

#include <algorithm>

#include <stdint.h>
#include <string.h>

using namespace v8;
using namespace node;

#define CHECK_CONDITION(expr, str) if(!(expr)) return Nan::ThrowError(str);

typedef enum ENDIANESS
{
    ENDIANESS_MOTOROLA = 0,
    ENDIANESS_INTEL
} ENDIANESS;

//-----------------------------------------------------------------------------------------
// _signals.* methods

static u_int64_t _getvalue(u_int8_t * data,
                           u_int32_t offset,
                           u_int32_t length,
                           ENDIANESS byteOrder)
{
    uint64_t d;
    uint64_t o = 0;

    if (byteOrder == ENDIANESS_INTEL) {
        d = le64toh(*((uint64_t *)&data[0]));
    } else {
        d = be64toh(*((uint64_t *)&data[0]));
    }

    uint64_t m;
    if (length == 64) {
      m = (uint64_t) UINT64_MAX;
    } else {
      m = (1LLU << length) - 1;
    }
    size_t shift;
    if (byteOrder == ENDIANESS_INTEL) {
        shift = offset;
    } else {
        shift = 64 - offset - length;
    }

    o = (d >> shift) & m;

#ifdef KAYAK_DATA_CHECK
    size_t i;
    int bitNr;
    uint64_t val = 0;
    if (byteOrder == ENDIANESS_INTEL) {
        for (i = 0; i < length; i++) {
            bitNr = i + offset;
            val |= ((data[bitNr >> 3] >> (bitNr & 0x07)) & 1) << i;
        }
    } else {
        for (i = 0; i < length; i++) {
            bitNr = offset + length - i -1;
            val |= ((data[bitNr >> 3] >> (7-(bitNr & 0x07))) & 1) << i;
        }
    }

    if (val != o) {
        fprintf(stderr, "getvalue: got %lu, expected %lu\n", val, o);
    }
#endif

    return o;
}

// Decode signal according description
// arg[0] - Data array
// arg[1] - offset zero indexed
// arg[3] - bitLength one indexed
// arg[4] - endianess
NAN_METHOD(DecodeSignal)
{
    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    bool isSigned = false;
    u_int8_t data[8];

    CHECK_CONDITION(info.Length() == 5, "Too few arguments");
    CHECK_CONDITION(info[0]->IsObject(), "Invalid argument");

    Local<Object> jsData = info[0]->ToObject();

    CHECK_CONDITION(Buffer::HasInstance(jsData), "Invalid argument");
    CHECK_CONDITION(info[1]->IsUint32(), "Invalid offset");
    CHECK_CONDITION(info[2]->IsUint32(), "Invalid bit length");
    CHECK_CONDITION(info[3]->IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4]->IsBoolean(), "Invalid signed flag");

    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    offset    = info[1]->ToUint32(context).ToLocalChecked()->Value();
    bitLength = info[2]->ToUint32(context).ToLocalChecked()->Value();
    endianess = info[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;
    isSigned  = info[4]->IsTrue() ? true : false;

    size_t maxBytes = std::min<u_int32_t>(Buffer::Length(jsData), sizeof(data));

    memset(data, 0, sizeof(data));
    memcpy(data, Buffer::Data(jsData), maxBytes);

    Local<Integer> retval;
    uint64_t val = _getvalue(data, offset, bitLength, endianess);

    // Value shall be interpreted as signed (2's complement)
    if (isSigned && val & (1LLU << (bitLength - 1))) {
        int32_t tmp = -1 * (~((UINT64_MAX << bitLength) | val) + 1);
        retval = Nan::New(tmp);
    } else {
        retval = Nan::New((u_int32_t)val);
    }

    Local<Array> raw_values = Nan::New<v8::Array>(2);
    Nan::Set(raw_values, 0, retval);
    Nan::Set(raw_values, 1, Nan::New((u_int32_t) (val >> 32)));

    info.GetReturnValue().Set(raw_values);
}

void _setvalue(u_int32_t offset, u_int32_t bitLength, ENDIANESS endianess, u_int8_t data[8], u_int64_t raw_value)
{
    uint64_t o;
    if (endianess == ENDIANESS_INTEL) {
        o = le64toh(*((uint64_t *)&data[0]));
    } else {
        o = be64toh(*((uint64_t *)&data[0]));
    }

    uint64_t m = 0;
    if (bitLength == 64) {
      m = (uint64_t) UINT64_MAX;
    } else {
      m = (1LLU << bitLength) - 1;
    }
    size_t shift;
    if (endianess == ENDIANESS_INTEL) {
        shift = offset;
    } else {
        shift = 64 - offset - bitLength;
    }

    o &= (uint64_t) ~(m << shift);
    o |= (uint64_t) (raw_value & m) << shift;
    /* fprintf(stdout, "raw %llu, output %llu, mask %llu shift %llu", raw_value, o, m, shift); */

    if (endianess == ENDIANESS_INTEL) {
        o = htole64(o);
    } else {
        o = htobe64(o);
    }

    memcpy(&data[0], &o, 8);

#ifdef KAYAK_DATA_CHECK
    size_t i;
    int bitNr;
    uint64_t val = 0;
    if (endianess == ENDIANESS_INTEL) {
        for (i = 0; i < bitLength; i++) {
            bitNr = i + offset;
            val |= ((data[bitNr >> 3] >> (bitNr & 0x07)) & 1) << i;
        }
    } else {
        for (i = 0; i < bitLength; i++) {
            bitNr = offset + bitLength - i -1;
            val |= ((data[bitNr >> 3] >> (7-(bitNr & 0x07))) & 1) << i;
        }
    }
    if(val != ( raw_value & m)) {
        fprintf(stderr, "setvalue: got %lu, expected %lu\n", val, raw_value & m);
    }
#endif
}

// Encode signal according description
// arg[0] - Data array
// arg[1] - bitOffset
// arg[2] - bitLength
// arg[3] - endianess
// arg[4] - sign flag
// arg[5] - first 4 bytes value to encode
// arg[6] - second 4 bytes value to encode
NAN_METHOD(EncodeSignal)
{
    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    bool sign = false;
    u_int8_t data[8];
    u_int64_t raw_value;

    CHECK_CONDITION(info.Length() >= 6, "Too few arguments");
    CHECK_CONDITION(info[0]->IsObject(), "Invalid argument");

    Local<Object> jsData = info[0]->ToObject();

    CHECK_CONDITION(Buffer::HasInstance(jsData), "Invalid argument");
    CHECK_CONDITION(info[1]->IsUint32(), "Invalid offset");
    CHECK_CONDITION(info[2]->IsUint32(), "Invalid bit length");
    CHECK_CONDITION(info[3]->IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4]->IsBoolean(), "Invalid sign flag");
    CHECK_CONDITION(info[5]->IsNumber() || info[5]->IsBoolean(), "Invalid value");

    Local<Context> context = info.GetIsolate()->GetCurrentContext();
    offset = info[1]->ToUint32(context).ToLocalChecked()->Value();
    bitLength = info[2]->ToUint32(context).ToLocalChecked()->Value();
    endianess = info[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;

    raw_value = info[5]->ToNumber(context).ToLocalChecked()->ToUint32(context).ToLocalChecked()->Value();
    if (info[6]->IsNumber() || info[6]->IsBoolean()) {
      raw_value += ((u_int64_t) info[6]->ToNumber(context).ToLocalChecked()->ToUint32(context).ToLocalChecked()->Value()) << 32;
    }

    size_t maxBytes = std::min<u_int64_t>(Buffer::Length(jsData), sizeof(data));

    // Since call may not have supplied enough bytes we have to make a temp copy
    memcpy(data, Buffer::Data(jsData), maxBytes);

    _setvalue(offset, bitLength, endianess, data, raw_value);

    memcpy(Buffer::Data(jsData), data, maxBytes);

    info.GetReturnValue().Set(Nan::Undefined());
}

//-----------------------------------------------------------------------------------------

NAN_MODULE_INIT(InitAll)
{
  Nan::Set(target, Nan::New<String>("decode_signal").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(DecodeSignal)).ToLocalChecked());
  Nan::Set(target, Nan::New<String>("encode_signal").ToLocalChecked(),
    Nan::GetFunction(Nan::New<FunctionTemplate>(EncodeSignal)).ToLocalChecked());
}

NODE_MODULE(can_signals, InitAll);
