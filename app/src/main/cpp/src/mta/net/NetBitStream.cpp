#include "NetBitStream.h"
#include <algorithm>

NetBitStream::NetBitStream()
    : m_writeBitOffset(0), m_readBitOffset(0) {
    m_buffer.reserve(256);
}

NetBitStream::NetBitStream(const uint8_t* data, size_t sizeBytes)
    : m_writeBitOffset(sizeBytes * 8), m_readBitOffset(0) {
    m_buffer.assign(data, data + sizeBytes);
}

void NetBitStream::Clear() {
    m_buffer.clear();
    m_writeBitOffset = 0;
    m_readBitOffset = 0;
}

void NetBitStream::WriteBit(bool bit) {
    size_t byteIndex = m_writeBitOffset / 8;
    size_t bitIndex = m_writeBitOffset % 8;

    if (byteIndex >= m_buffer.size()) {
        m_buffer.push_back(0);
    }

    if (bit) {
        m_buffer[byteIndex] |= (1 << (7 - bitIndex));
    } else {
        m_buffer[byteIndex] &= ~(1 << (7 - bitIndex));
    }

    m_writeBitOffset++;
}

void NetBitStream::WriteBits(const uint8_t* input, size_t numBits) {
    for (size_t i = 0; i < numBits; ++i) {
        size_t byteIdx = i / 8;
        size_t bitIdx = i % 8;
        bool bit = (input[byteIdx] & (1 << (7 - bitIdx))) != 0;
        WriteBit(bit);
    }
}

void NetBitStream::WriteByte(uint8_t val) {
    WriteBits(&val, 8);
}

void NetBitStream::WriteShort(uint16_t val) {
    uint8_t bytes[2];
    bytes[0] = static_cast<uint8_t>(val & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    WriteBits(bytes, 16);
}

void NetBitStream::WriteInt(uint32_t val) {
    uint8_t bytes[4];
    bytes[0] = static_cast<uint8_t>(val & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    bytes[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    bytes[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    WriteBits(bytes, 32);
}

void NetBitStream::WriteInt64(uint64_t val) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
    }
    WriteBits(bytes, 64);
}

void NetBitStream::WriteFloat(float val) {
    uint32_t intVal;
    std::memcpy(&intVal, &val, sizeof(float));
    WriteInt(intVal);
}

void NetBitStream::WriteString(const std::string& str) {
    uint16_t length = static_cast<uint16_t>(str.length());
    WriteShort(length);
    if (length > 0) {
        WriteBytes(reinterpret_cast<const uint8_t*>(str.data()), length);
    }
}

void NetBitStream::WriteBytes(const uint8_t* data, size_t length) {
    WriteBits(data, length * 8);
}

bool NetBitStream::ReadBit(bool& outBit) {
    if (m_readBitOffset >= m_writeBitOffset) {
        return false;
    }

    size_t byteIndex = m_readBitOffset / 8;
    size_t bitIndex = m_readBitOffset % 8;

    outBit = (m_buffer[byteIndex] & (1 << (7 - bitIndex))) != 0;
    m_readBitOffset++;
    return true;
}

bool NetBitStream::ReadBits(uint8_t* output, size_t numBits) {
    if (m_readBitOffset + numBits > m_writeBitOffset) {
        return false;
    }

    std::memset(output, 0, (numBits + 7) / 8);
    for (size_t i = 0; i < numBits; ++i) {
        bool bit = false;
        ReadBit(bit);
        if (bit) {
            output[i / 8] |= (1 << (7 - (i % 8)));
        }
    }
    return true;
}

bool NetBitStream::ReadByte(uint8_t& outVal) {
    return ReadBits(&outVal, 8);
}

bool NetBitStream::ReadShort(uint16_t& outVal) {
    uint8_t bytes[2];
    if (!ReadBits(bytes, 16)) return false;
    outVal = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

bool NetBitStream::ReadInt(uint32_t& outVal) {
    uint8_t bytes[4];
    if (!ReadBits(bytes, 32)) return false;
    outVal = static_cast<uint32_t>(bytes[0]) |
             (static_cast<uint32_t>(bytes[1]) << 8) |
             (static_cast<uint32_t>(bytes[2]) << 16) |
             (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool NetBitStream::ReadInt64(uint64_t& outVal) {
    uint8_t bytes[8];
    if (!ReadBits(bytes, 64)) return false;
    outVal = 0;
    for (int i = 0; i < 8; ++i) {
        outVal |= (static_cast<uint64_t>(bytes[i]) << (i * 8));
    }
    return true;
}

bool NetBitStream::ReadFloat(float& outVal) {
    uint32_t intVal;
    if (!ReadInt(intVal)) return false;
    std::memcpy(&outVal, &intVal, sizeof(float));
    return true;
}

bool NetBitStream::ReadString(std::string& outStr) {
    uint16_t length = 0;
    if (!ReadShort(length)) return false;
    if (length == 0) {
        outStr.clear();
        return true;
    }

    std::vector<uint8_t> bytes(length);
    if (!ReadBytes(bytes.data(), length)) return false;
    outStr.assign(reinterpret_cast<const char*>(bytes.data()), length);
    return true;
}

bool NetBitStream::ReadBytes(uint8_t* output, size_t length) {
    return ReadBits(output, length * 8);
}
