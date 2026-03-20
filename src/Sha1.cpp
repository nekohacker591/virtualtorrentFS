#include "Sha1.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace vtfs {

namespace {
constexpr std::uint32_t leftRotate(std::uint32_t value, std::uint32_t bits) {
    return (value << bits) | (value >> (32 - bits));
}
}

Sha1::Sha1() : state_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u} {}

void Sha1::update(const std::uint8_t* data, std::size_t size) {
    bitCount_ += static_cast<std::uint64_t>(size) * 8ULL;
    while (size > 0) {
        const auto chunk = std::min<std::size_t>(size, buffer_.size() - bufferSize_);
        std::memcpy(buffer_.data() + bufferSize_, data, chunk);
        bufferSize_ += chunk;
        data += chunk;
        size -= chunk;
        if (bufferSize_ == buffer_.size()) {
            processBlock(buffer_.data());
            bufferSize_ = 0;
        }
    }
}

void Sha1::update(std::string_view text) {
    update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

std::array<std::uint8_t, 20> Sha1::final() {
    buffer_[bufferSize_++] = 0x80;
    if (bufferSize_ > 56) {
        while (bufferSize_ < 64) {
            buffer_[bufferSize_++] = 0;
        }
        processBlock(buffer_.data());
        bufferSize_ = 0;
    }
    while (bufferSize_ < 56) {
        buffer_[bufferSize_++] = 0;
    }
    for (int i = 7; i >= 0; --i) {
        buffer_[bufferSize_++] = static_cast<std::uint8_t>((bitCount_ >> (i * 8)) & 0xFFu);
    }
    processBlock(buffer_.data());
    bufferSize_ = 0;

    std::array<std::uint8_t, 20> out{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        out[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFu);
    }
    return out;
}

std::array<std::uint8_t, 20> Sha1::digest(std::string_view text) {
    Sha1 sha1;
    sha1.update(text);
    return sha1.final();
}

std::string Sha1::toHex(const std::array<std::uint8_t, 20>& digestBytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digestBytes) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

void Sha1::processBlock(const std::uint8_t* block) {
    std::uint32_t words[80]{};
    for (int i = 0; i < 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        words[i] = leftRotate(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];

    for (int i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const auto temp = leftRotate(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = leftRotate(b, 30);
        b = a;
        a = temp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

} // namespace vtfs
