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
#include <napi.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

#define CHECK_CONDITION(expr, str) \
  if (!(expr)) { \
    Napi::TypeError::New(env, str).ThrowAsJavaScriptException(); \
    return env.Undefined(); \
  }

enum class ENDIANESS
{
    MOTOROLA = 0,
    INTEL
};

enum class SIGNAL_TYPE
{
    UNSIGNED = 0,
    SIGNED   = 1,
    FLOAT32  = 2,
    FLOAT64  = 3
};

// Max CAN-FD payload. The local scratch buffer is padded so that the 8-byte
// windowed memcpy used by _getvalue / _setvalue always has 8 bytes available,
// even for a signal sitting in the final bytes of the payload. The padding
// stays zero and any garbage bits inside the window are masked off before use.
constexpr size_t   MAX_PAYLOAD_BYTES = 64;
constexpr size_t   WINDOW_SAFETY_PAD = 8;
constexpr size_t   LOCAL_BUFFER_SIZE = MAX_PAYLOAD_BYTES + WINDOW_SAFETY_PAD;
constexpr uint32_t MAX_TOTAL_BITS    = MAX_PAYLOAD_BYTES * 8;

static SIGNAL_TYPE _parse_signal_type(Napi::Env env, const Napi::Value& v)
{
    if (v.IsBoolean())
        return v.As<Napi::Boolean>().Value() ? SIGNAL_TYPE::SIGNED : SIGNAL_TYPE::UNSIGNED;
    uint32_t n = v.As<Napi::Number>().Uint32Value();
    if (n > static_cast<uint32_t>(SIGNAL_TYPE::FLOAT64)) {
        Napi::TypeError::New(env, "Unknown signal type").ThrowAsJavaScriptException();
        return SIGNAL_TYPE::UNSIGNED;
    }
    return static_cast<SIGNAL_TYPE>(n);
}

// Returns the fixed IEEE-754 bit width for float signal types; 0 for integer types.
static uint32_t signal_type_bit_width(SIGNAL_TYPE t)
{
    switch (t) {
        case SIGNAL_TYPE::FLOAT32: return 32;
        case SIGNAL_TYPE::FLOAT64: return 64;
        default:                   return 0;
    }
}

//-----------------------------------------------------------------------------------------
// _signals.* methods

// Extract a 1..64-bit signal value from a CAN/CAN-FD payload.
//
// Fast path: when the signal's bits all fit inside the 8-byte window starting
// at data[offset/8], one memcpy + endian swap + shift + mask suffices. This
// covers every signal with length <= 57 and every byte-aligned signal up to 64
// bits — i.e. essentially everything seen in real CAN databases. The
// LOCAL_BUFFER_SIZE padding guarantees the 8-byte read is always in-bounds.
//
// Fallback: a 58..64 bit signal at a non-byte-aligned offset would straddle
// more than 8 bytes worth of bit positions; in that rare case we walk it
// bit by bit.
[[nodiscard]] static uint64_t _getvalue(const uint8_t* data,
                                        uint32_t offset,
                                        uint32_t length,
                                        ENDIANESS byteOrder)
{
    const uint32_t bit_in_byte = offset % 8;
    const size_t   byte_start  = offset / 8;
    const uint64_t mask        = (length == 64) ? UINT64_MAX
                                                : ((UINT64_C(1) << length) - 1);

    if (bit_in_byte + length <= 64) {
        uint64_t d_raw;
        std::memcpy(&d_raw, &data[byte_start], sizeof(d_raw));
        if (byteOrder == ENDIANESS::INTEL) {
            return (le64toh(d_raw) >> bit_in_byte) & mask;
        }
        return (be64toh(d_raw) >> (64 - bit_in_byte - length)) & mask;
    }

    uint64_t val = 0;
    if (byteOrder == ENDIANESS::INTEL) {
        for (uint32_t i = 0; i < length; i++) {
            uint32_t bit = offset + i;
            val |= static_cast<uint64_t>((data[bit >> 3] >> (bit & 7)) & 1) << i;
        }
    } else {
        for (uint32_t i = 0; i < length; i++) {
            uint32_t bit = offset + length - i - 1;
            val |= static_cast<uint64_t>((data[bit >> 3] >> (7 - (bit & 7))) & 1) << i;
        }
    }
    return val;
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
    uint32_t offset, bitLength;
    ENDIANESS endianess;
    uint8_t data[LOCAL_BUFFER_SIZE];

    CHECK_CONDITION(info.Length() == 5, "Too few arguments");
    CHECK_CONDITION(info[0].IsBuffer(), "Invalid argument");
    CHECK_CONDITION(info[1].IsNumber(), "Invalid offset");
    CHECK_CONDITION(info[2].IsNumber(), "Invalid bit length");
    CHECK_CONDITION(info[3].IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(info[4].IsNumber() || info[4].IsBoolean(), "Invalid type");

    Napi::Buffer<uint8_t> jsData = info[0].As<Napi::Buffer<uint8_t>>();

    offset     = info[1].As<Napi::Number>().Uint32Value();
    bitLength  = info[2].As<Napi::Number>().Uint32Value();
    endianess  = info[3].As<Napi::Boolean>().Value() ? ENDIANESS::INTEL : ENDIANESS::MOTOROLA;
    SIGNAL_TYPE signalType = _parse_signal_type(env, info[4]);
    if (env.IsExceptionPending()) return env.Undefined();

    // Float types have a fixed width dictated by the type, not the caller.
    // Using signal_type_bit_width() ensures encode and decode always agree.
    uint32_t width = signal_type_bit_width(signalType);
    uint32_t effectiveBitLength = (width > 0) ? width : bitLength;

    CHECK_CONDITION(effectiveBitLength > 0 && effectiveBitLength <= 64,
                    "bitLength must be in range 1..64");
    CHECK_CONDITION(offset + effectiveBitLength <= MAX_TOTAL_BITS,
                    "signal extends past 64-byte frame");

    size_t maxBytes = std::min<size_t>(jsData.ByteLength(), MAX_PAYLOAD_BYTES);
    CHECK_CONDITION((offset + effectiveBitLength + 7) / 8 <= maxBytes,
                    "signal extends past buffer");

    std::memset(data, 0, sizeof(data));
    std::memcpy(data, jsData.Data(), maxBytes);

    uint64_t val = _getvalue(data, offset, effectiveBitLength, endianess);

    Napi::Value retval;
    if (signalType == SIGNAL_TYPE::FLOAT32) {
        // Reinterpret the 32 raw bits as IEEE-754 single precision.
        // _getvalue already applied the correct byte order, so std::bit_cast
        // gives the right value on any host endianness.
        retval = Napi::Number::New(env,
            static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(val))));
    } else if (signalType == SIGNAL_TYPE::FLOAT64) {
        retval = Napi::Number::New(env, std::bit_cast<double>(val));
    } else if (signalType == SIGNAL_TYPE::SIGNED && (val & (UINT64_C(1) << (bitLength - 1)))) {
        // Sign-extend from bitLength bits to int64_t.
        // XOR-subtract avoids UB that a naive left-shift approach would have.
        uint64_t sign_mask = UINT64_C(1) << (bitLength - 1);
        int64_t tmp = static_cast<int64_t>((val ^ sign_mask) - sign_mask);
        retval = Napi::Number::New(env, static_cast<double>(tmp));
    } else {
        retval = Napi::Number::New(env, static_cast<uint32_t>(val));
    }

    Napi::Array raw_values = Napi::Array::New(env, 2);
    raw_values.Set(0u, retval);
    // For float types the full value is in index 0; index 1 is always 0.
    uint32_t hi = (width > 0) ? 0u : static_cast<uint32_t>(val >> 32);
    raw_values.Set(1u, Napi::Number::New(env, hi));

    return raw_values;
}

// Write a 1..64-bit signal value into a CAN/CAN-FD payload.
//
// Same fast-path / fallback split as _getvalue. The fast path is a read-
// modify-write of the 8-byte window starting at data[offset/8]; non-signal
// bits inside the window are preserved by the RMW.
static void _setvalue(uint32_t offset, uint32_t bitLength, ENDIANESS endianess,
                      uint8_t* data, uint64_t raw_value)
{
    const uint32_t bit_in_byte = offset % 8;
    const size_t   byte_start  = offset / 8;
    const uint64_t mask        = (bitLength == 64) ? UINT64_MAX
                                                   : ((UINT64_C(1) << bitLength) - 1);

    if (bit_in_byte + bitLength <= 64) {
        uint64_t o_raw;
        std::memcpy(&o_raw, &data[byte_start], sizeof(o_raw));

        if (endianess == ENDIANESS::INTEL) {
            uint64_t o = le64toh(o_raw);
            o &= ~(mask << bit_in_byte);
            o |= (raw_value & mask) << bit_in_byte;
            o_raw = htole64(o);
        } else {
            uint64_t o = be64toh(o_raw);
            const uint32_t shift = 64 - bit_in_byte - bitLength;
            o &= ~(mask << shift);
            o |= (raw_value & mask) << shift;
            o_raw = htobe64(o);
        }

        std::memcpy(&data[byte_start], &o_raw, sizeof(o_raw));
        return;
    }

    if (endianess == ENDIANESS::INTEL) {
        for (uint32_t i = 0; i < bitLength; i++) {
            uint32_t bit       = offset + i;
            uint32_t b_in_byte = bit & 7;
            uint8_t  v         = (raw_value >> i) & 1;
            data[bit >> 3] = (data[bit >> 3] & ~(1u << b_in_byte)) | (v << b_in_byte);
        }
    } else {
        for (uint32_t i = 0; i < bitLength; i++) {
            uint32_t bit       = offset + bitLength - i - 1;
            uint32_t b_in_byte = 7 - (bit & 7);
            uint8_t  v         = (raw_value >> i) & 1;
            data[bit >> 3] = (data[bit >> 3] & ~(1u << b_in_byte)) | (v << b_in_byte);
        }
    }
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
    uint32_t offset, bitLength;
    ENDIANESS endianess;
    uint8_t data[LOCAL_BUFFER_SIZE];

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
    endianess  = info[3].As<Napi::Boolean>().Value() ? ENDIANESS::INTEL : ENDIANESS::MOTOROLA;
    SIGNAL_TYPE signalType = _parse_signal_type(env, info[4]);
    if (env.IsExceptionPending()) return env.Undefined();

    uint32_t width = signal_type_bit_width(signalType);
    uint32_t effectiveBitLength = (width > 0) ? width : bitLength;

    CHECK_CONDITION(effectiveBitLength > 0 && effectiveBitLength <= 64,
                    "bitLength must be in range 1..64");
    CHECK_CONDITION(offset + effectiveBitLength <= MAX_TOTAL_BITS,
                    "signal extends past 64-byte frame");

    size_t maxBytes = std::min<size_t>(jsData.ByteLength(), MAX_PAYLOAD_BYTES);
    CHECK_CONDITION((offset + effectiveBitLength + 7) / 8 <= maxBytes,
                    "signal extends past buffer");

    // memset before memcpy: _setvalue's fast-path RMW may touch bytes past
    // maxBytes inside the local buffer, and we don't want to OR-in stack
    // garbage. (The trailing bytes are discarded by the memcpy-back below.)
    std::memset(data, 0, sizeof(data));
    std::memcpy(data, jsData.Data(), maxBytes);

    if (signalType == SIGNAL_TYPE::FLOAT32) {
        float f = static_cast<float>(info[5].As<Napi::Number>().DoubleValue());
        _setvalue(offset, effectiveBitLength, endianess, data,
                  static_cast<uint64_t>(std::bit_cast<uint32_t>(f)));
    } else if (signalType == SIGNAL_TYPE::FLOAT64) {
        double d = info[5].As<Napi::Number>().DoubleValue();
        _setvalue(offset, effectiveBitLength, endianess, data,
                  std::bit_cast<uint64_t>(d));
    } else {
        uint64_t raw_value = static_cast<uint64_t>(info[5].As<Napi::Number>().Uint32Value());
        if (info.Length() > 6 && (info[6].IsNumber() || info[6].IsBoolean())) {
            raw_value += static_cast<uint64_t>(info[6].As<Napi::Number>().Uint32Value()) << 32;
        }
        _setvalue(offset, effectiveBitLength, endianess, data, raw_value);
    }

    std::memcpy(jsData.Data(), data, maxBytes);

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
