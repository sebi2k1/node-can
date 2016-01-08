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
    uint64_t d = be64toh(*((uint64_t *)&data[0]));
    uint64_t o = 0;

    if (byteOrder == ENDIANESS_INTEL)
    {
        d <<= offset;

        size_t i, left = length;

        for (i = 0; i < length;)
        {
            size_t next_shift = left >= 8 ? 8 : left;
            size_t shift = 64 - (i + next_shift);
            size_t m = next_shift < 8 ? 0xFF >> next_shift : 0xFF;

            o |= ((d >> shift) & m) << i;

            left -= 8;
            i += next_shift;
        }
    }
    else
    {
        uint64_t m = UINT64_MAX;
        size_t shift = 64 - offset - 1;

        m = (1 << length) - 1;
        o = (d >> shift) & m;
    }

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

    offset    = info[1]->ToUint32()->Uint32Value();
    bitLength = info[2]->ToUint32()->Uint32Value();
    endianess = info[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;
    isSigned  = info[4]->IsTrue() ? true : false;

    size_t maxBytes = std::min<u_int32_t>(Buffer::Length(jsData), sizeof(data));

    memset(data, 0, sizeof(data));
    memcpy(data, Buffer::Data(jsData), maxBytes);

    Local<Integer> retval;
    uint64_t val = _getvalue(data, offset, bitLength, endianess);

    // Value shall be interpreted as signed (2's complement)
    if (isSigned && val & (1 << (bitLength - 1))) {
        int32_t tmp = -1 * (~((UINT64_MAX << bitLength) | val) + 1);
        retval = Nan::New(tmp);
    } else {
        retval = Nan::New((u_int32_t)val);
    }

    info.GetReturnValue().Set(retval);
}

void _setvalue(u_int32_t offset, u_int32_t bitLength, ENDIANESS endianess, u_int8_t data[8], u_int64_t raw_value)
{
    uint64_t o = be64toh(*(uint64_t *)&data[0]);

    if (endianess == ENDIANESS_INTEL)
    {
        size_t left = bitLength;

        size_t source = 0;

        for (source = 0; source < bitLength; )
        {
            size_t next_shift = left < 8 ? left : 8;
            size_t shift = (64 - offset - next_shift) - source;
            uint64_t m = ((1 << next_shift) - 1);

            o &= ~(m << shift);
            o |= (raw_value & m) << shift;

            raw_value >>= 8;
            source += next_shift;
            left -= next_shift;
        }
    }
    else
    {
        uint64_t m = ((1 << bitLength) - 1);
        size_t shift = 64 - offset - 1;

        o &= ~(m << shift);
        o |= (raw_value & m) << shift;
    }

    o = htobe64(o);

    memcpy(&data[0], &o, 8);
}

// Encode signal according description
// arg[0] - Data array
// arg[1] - startByte one indexed, One indexed, Left(1)->Right(8)
// arg[2] - startBit zero indexed, Right(0)->Left(7)
// arg[3] - bitLength one indexed
// arg[4] - endianess
// arg[5] - sign flag
// arg[6] - value to encode
NAN_METHOD(EncodeSignal)
{
    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    bool sign = false;
    u_int8_t data[8];
    u_int64_t raw_value;

    CHECK_CONDITION(info.Length() == 6, "Too few arguments");
    CHECK_CONDITION(info[0]->IsObject(), "Invalid argument");

    Local<Object> jsData = info[0]->ToObject();

    CHECK_CONDITION(Buffer::HasInstance(jsData), "Invalid argument");
    CHECK_CONDITION(info[1]->IsUint32(), "Invalid offset");
    CHECK_CONDITION(info[2]->IsUint32(), "Invalid bit length");
    CHECK_CONDITION(info[3]->IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4]->IsBoolean(), "Invalid sign flag");
    CHECK_CONDITION(info[5]->IsNumber() || info[5]->IsBoolean(), "Invalid value");

    offset = info[1]->ToUint32()->Uint32Value();
    bitLength = info[2]->ToUint32()->Uint32Value();
    endianess = info[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;
    sign = info[4]->IsTrue() ? true : false;

    if (sign) {
        int32_t in_val = info[5]->ToNumber()->Int32Value();

        if (in_val < 0) {
            in_val *= -1; // Make it a positive number

            raw_value = (~in_val) + 1;
            raw_value &= ~(UINT64_MAX << bitLength); // mask valid bits
        }
    }

    raw_value = info[5]->ToNumber()->Uint32Value();

    size_t maxBytes = std::min<u_int32_t>(Buffer::Length(jsData), sizeof(data));

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
