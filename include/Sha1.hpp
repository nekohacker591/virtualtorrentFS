#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vtfs {

class Sha1 {
  public:
    Sha1();
    void update(const std::uint8_t* data, std::size_t size);
    void update(std::string_view text);
    [[nodiscard]] std::array<std::uint8_t, 20> final();
    [[nodiscard]] static std::array<std::uint8_t, 20> digest(std::string_view text);
    [[nodiscard]] static std::string toHex(const std::array<std::uint8_t, 20>& digestBytes);

  private:
    void processBlock(const std::uint8_t* block);

    std::array<std::uint32_t, 5> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bitCount_ = 0;
    std::size_t bufferSize_ = 0;
};

} // namespace vtfs
