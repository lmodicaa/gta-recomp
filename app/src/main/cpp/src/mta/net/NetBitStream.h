#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

class NetBitStream {
public:
    NetBitStream();
    NetBitStream(const uint8_t* data, size_t sizeBytes);
    ~NetBitStream() = default;

    // Writing primitives
    void WriteBit(bool bit);
    void WriteBits(const uint8_t* input, size_t numBits);
    void WriteByte(uint8_t val);
    void WriteShort(uint16_t val);
    void WriteInt(uint32_t val);
    void WriteInt64(uint64_t val);
    void WriteFloat(float val);
    void WriteString(const std::string& str);
    void WriteBytes(const uint8_t* data, size_t length);

    // Reading primitives
    bool ReadBit(bool& outBit);
    bool ReadBits(uint8_t* output, size_t numBits);
    bool ReadByte(uint8_t& outVal);
    bool ReadShort(uint16_t& outVal);
    bool ReadInt(uint32_t& outVal);
    bool ReadInt64(uint64_t& outVal);
    bool ReadFloat(float& outVal);
    bool ReadString(std::string& outStr);
    bool ReadBytes(uint8_t* output, size_t length);

    // Stream info
    size_t GetNumberOfBitsUsed() const { return m_writeBitOffset; }
    size_t GetNumberOfBytesUsed() const { return (m_writeBitOffset + 7) / 8; }
    size_t GetNumberOfUnreadBits() const { return m_writeBitOffset > m_readBitOffset ? m_writeBitOffset - m_readBitOffset : 0; }
    const uint8_t* GetData() const { return m_buffer.data(); }

    void ResetReadPointer() { m_readBitOffset = 0; }
    void Clear();

private:
    std::vector<uint8_t> m_buffer;
    size_t m_writeBitOffset;
    size_t m_readBitOffset;
};
