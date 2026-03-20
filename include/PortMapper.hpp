#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

struct PortMappingResult {
    std::uint16_t externalPort = 0;
    std::uint32_t lifetimeSeconds = 0;
};

class PortMapper {
  public:
    [[nodiscard]] std::vector<std::uint8_t> buildNatPmpMapRequest(std::uint16_t internalPort,
                                                                  std::uint16_t requestedExternalPort,
                                                                  std::uint32_t lifetimeSeconds) const;
    [[nodiscard]] std::vector<std::uint8_t> buildPcpMapRequest(std::uint16_t internalPort,
                                                               std::uint16_t requestedExternalPort,
                                                               std::uint32_t lifetimeSeconds) const;
    [[nodiscard]] std::string buildSsdpSearchRequest() const;
    [[nodiscard]] std::string buildUpnpAddPortMappingSoap(std::uint16_t internalPort, std::uint16_t externalPort, std::string_view localAddress) const;
    [[nodiscard]] std::string buildUpnpDeletePortMappingSoap(std::uint16_t externalPort) const;
    [[nodiscard]] std::optional<PortMappingResult> parseNatPmpMapResponse(const std::vector<std::uint8_t>& packet) const;
};

} // namespace vtfs
