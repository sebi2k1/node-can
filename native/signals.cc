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

#include <napi.h>

#include <algorithm>

#include <stdint.h>
#include <string.h>

#define CHECK_CONDITION(expr, str) \
  if (!(expr)) { \
    Napi::TypeError::New(env, str).ThrowAsJavaScriptException(); \
    return env.Undefined(); \
  }

typedef enum ENDIANESS
{
    ENDIANESS_MOTOROLA = 0,
    ENDIANESS_INTEL
} ENDIANESS;

typedef enum SIGNAL_TYPE
{
    SIGNAL_TYPE_UNSIGNED = 0,
    SIGNAL_TYPE_SIGNED   = 1,
    SIGNAL_TYPE_FLOAT32  = 2,
    SIGNAL_TYPE_FLOAT64  = 3
} SIGNAL_TYPE;

static SIGNAL_TYPE _parse_signal_type(const Napi::Value& v)
{
    if (v.IsBoolean())
        return v.As<Napi::Boolean>().Value() ? SIGNAL_TYPE_SIGNED : SIGNAL_TYPE_UNSIGNED;
    return (SIGNAL_TYPE)v.As<Napi::Number>().Uint32Value();
}

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
// arg[2] - bitLength one indexed
// arg[3] - endianess (bool: true=Intel/LE, false=Motorola/BE)
// arg[4] - signal type: bool (false=unsigned, true=signed) or number (0=unsigned, 1=signed, 2=float32, 3=float64)
Napi::Value DecodeSignal(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();
    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    u_int8_t data[64];           // CANFD size of bufer = 64

    CHECK_CONDITION(info.Length() == 5, "Too few arguments");
    CHECK_CONDITION(info[0].IsBuffer(), "Invalid argument");
    CHECK_CONDITION(info[1].IsNumber(), "Invalid offset");
    CHECK_CONDITION(info[2].IsNumber(), "Invalid bit length");
    CHECK_CONDITION(info[3].IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4].IsNumber() || info[4].IsBoolean(), "Invalid type");

    Napi::Buffer<uint8_t> jsData = info[0].As<Napi::Buffer<uint8_t>>();

    offset     = info[1].As<Napi::Number>().Uint32Value();
    bitLength  = info[2].As<Napi::Number>().Uint32Value();
    endianess  = info[3].As<Napi::Boolean>().Value() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;
    SIGNAL_TYPE signalType = _parse_signal_type(info[4]);

    size_t maxBytes = std::min<size_t>(jsData.ByteLength(), sizeof(data));

    memset(data, 0, sizeof(data));
    memcpy(data, jsData.Data(), maxBytes);

    uint64_t val = _getvalue(data, offset, bitLength, endianess);

    Napi::Value retval;
    if (signalType == SIGNAL_TYPE_FLOAT32) {
        // Reinterpret the 32 raw bits as IEEE-754 single precision.
        // _getvalue already applied the correct byte order, so a plain
        // memcpy into a float gives the right value on any host endianess.
        u_int32_t raw = (u_int32_t)val;
        float f;
        memcpy(&f, &raw, sizeof(f));
        retval = Napi::Number::New(env, (double)f);
    } else if (signalType == SIGNAL_TYPE_FLOAT64) {
        double d;
        memcpy(&d, &val, sizeof(d));
        retval = Napi::Number::New(env, d);
    } else if (signalType == SIGNAL_TYPE_SIGNED && val & (1LLU << (bitLength - 1))) {
        int32_t tmp = -1 * (~((UINT64_MAX << bitLength) | val) + 1);
        retval = Napi::Number::New(env, tmp);
    } else {
        retval = Napi::Number::New(env, (u_int32_t)val);
    }

    Napi::Array raw_values = Napi::Array::New(env, 2);
    raw_values.Set(0u, retval);
    // For float types the value fits in index 0; index 1 is always 0.
    u_int32_t hi = (signalType == SIGNAL_TYPE_FLOAT32 || signalType == SIGNAL_TYPE_FLOAT64)
                   ? 0u : (u_int32_t)(val >> 32);
    raw_values.Set(1u, Napi::Number::New(env, hi));

    return raw_values;
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
// arg[3] - endianess (bool: true=Intel/LE, false=Motorola/BE)
// arg[4] - signal type: bool (false=unsigned, true=signed) or number (0=unsigned, 1=signed, 2=float32, 3=float64)
// arg[5] - value to encode (integer lo-word for int types; float/double JS number for float types)
// arg[6] - (integer types only) high 32 bits for 64-bit integers
Napi::Value EncodeSignal(const Napi::CallbackInfo& info)
{
    Napi::Env env = info.Env();
    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    u_int8_t data[64];           // CANFD size of bufer = 64

    CHECK_CONDITION(info.Length() >= 6, "Too few arguments");
    CHECK_CONDITION(info[0].IsBuffer(), "Invalid argument");
    CHECK_CONDITION(info[1].IsNumber(), "Invalid offset");
    CHECK_CONDITION(info[2].IsNumber(), "Invalid bit length");
    CHECK_CONDITION(info[3].IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4].IsNumber() || info[4].IsBoolean(), "Invalid type");
    CHECK_CONDITION(info[5].IsNumber() || info[5].IsBoolean(), "Invalid value");

    Napi::Buffer<uint8_t> jsData = info[0].As<Napi::Buffer<uint8_t>>();

    offset     = info[1].As<Napi::Number>().Uint32Value();
    bitLength  = info[2].As<Napi::Number>().Uint32Value();
    endianess  = info[3].As<Napi::Boolean>().Value() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;
    SIGNAL_TYPE signalType = _parse_signal_type(info[4]);

    size_t maxBytes = std::min<size_t>(jsData.ByteLength(), sizeof(data));
    memcpy(data, jsData.Data(), maxBytes);

    if (signalType == SIGNAL_TYPE_FLOAT32) {
        float f = (float)info[5].As<Napi::Number>().DoubleValue();
        u_int32_t raw;
        memcpy(&raw, &f, sizeof(raw));
        _setvalue(offset, 32, endianess, data, (u_int64_t)raw);
    } else if (signalType == SIGNAL_TYPE_FLOAT64) {
        double d = info[5].As<Napi::Number>().DoubleValue();
        u_int64_t raw;
        memcpy(&raw, &d, sizeof(raw));
        _setvalue(offset, 64, endianess, data, raw);
    } else {
        u_int64_t raw_value = info[5].As<Napi::Number>().Uint32Value();
        if (info.Length() > 6 && (info[6].IsNumber() || info[6].IsBoolean())) {
            raw_value += ((u_int64_t)info[6].As<Napi::Number>().Uint32Value()) << 32;
        }
        _setvalue(offset, bitLength, endianess, data, raw_value);
    }

    memcpy(jsData.Data(), data, maxBytes);

    return env.Undefined();
}

//-----------------------------------------------------------------------------------------

Napi::Object InitAll(Napi::Env env, Napi::Object exports)
{
    exports.Set("decodeSignal", Napi::Function::New(env, DecodeSignal));
    exports.Set("encodeSignal", Napi::Function::New(env, EncodeSignal));
    return exports;
}

NODE_API_MODULE(can_signals, InitAll)
