#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

struct DhtNode {
    std::string host;
    std::uint16_t port = 6881;
};

class DhtClient {
  public:
    DhtClient();

    [[nodiscard]] std::array<std::uint8_t, 20> nodeId() const;
    [[nodiscard]] std::vector<std::uint8_t> buildPingQuery(std::string_view txId) const;
    [[nodiscard]] std::vector<std::uint8_t> buildFindNodeQuery(std::string_view txId,
                                                               const std::array<std::uint8_t, 20>& targetId) const;
    [[nodiscard]] std::vector<DhtNode> defaultBootstrapNodes() const;

  private:
    std::array<std::uint8_t, 20> nodeId_{};
};

} // namespace vtfs
