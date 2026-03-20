#include "PortMapper.hpp"

#include <sstream>

namespace vtfs {

std::vector<std::uint8_t> PortMapper::buildNatPmpMapRequest(std::uint16_t internalPort,
                                                            std::uint16_t requestedExternalPort,
                                                            std::uint32_t lifetimeSeconds) const {
    std::vector<std::uint8_t> packet(12);
    packet[0] = 0;
    packet[1] = 2;
    packet[2] = 0;
    packet[3] = 0;
    packet[4] = static_cast<std::uint8_t>((internalPort >> 8) & 0xFF);
    packet[5] = static_cast<std::uint8_t>(internalPort & 0xFF);
    packet[6] = static_cast<std::uint8_t>((requestedExternalPort >> 8) & 0xFF);
    packet[7] = static_cast<std::uint8_t>(requestedExternalPort & 0xFF);
    packet[8] = static_cast<std::uint8_t>((lifetimeSeconds >> 24) & 0xFF);
    packet[9] = static_cast<std::uint8_t>((lifetimeSeconds >> 16) & 0xFF);
    packet[10] = static_cast<std::uint8_t>((lifetimeSeconds >> 8) & 0xFF);
    packet[11] = static_cast<std::uint8_t>(lifetimeSeconds & 0xFF);
    return packet;
}

std::vector<std::uint8_t> PortMapper::buildPcpMapRequest(std::uint16_t internalPort,
                                                         std::uint16_t requestedExternalPort,
                                                         std::uint32_t lifetimeSeconds) const {
    std::vector<std::uint8_t> packet(60, 0);
    packet[0] = 2;
    packet[1] = 1;
    packet[4] = static_cast<std::uint8_t>((lifetimeSeconds >> 24) & 0xFF);
    packet[5] = static_cast<std::uint8_t>((lifetimeSeconds >> 16) & 0xFF);
    packet[6] = static_cast<std::uint8_t>((lifetimeSeconds >> 8) & 0xFF);
    packet[7] = static_cast<std::uint8_t>(lifetimeSeconds & 0xFF);
    packet[40] = static_cast<std::uint8_t>((internalPort >> 8) & 0xFF);
    packet[41] = static_cast<std::uint8_t>(internalPort & 0xFF);
    packet[42] = static_cast<std::uint8_t>((requestedExternalPort >> 8) & 0xFF);
    packet[43] = static_cast<std::uint8_t>(requestedExternalPort & 0xFF);
    return packet;
}

std::string PortMapper::buildSsdpSearchRequest() const {
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
           "USER-AGENT: torrentfs\r\n\r\n";
}

std::string PortMapper::buildUpnpAddPortMappingSoap(std::uint16_t internalPort,
                                                    std::uint16_t externalPort,
                                                    std::string_view localAddress) const {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?>"
        << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        << "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        << "<s:Body><u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
        << "<NewRemoteHost></NewRemoteHost>"
        << "<NewExternalPort>" << externalPort << "</NewExternalPort>"
        << "<NewProtocol>TCP</NewProtocol>"
        << "<NewInternalPort>" << internalPort << "</NewInternalPort>"
        << "<NewInternalClient>" << localAddress << "</NewInternalClient>"
        << "<NewEnabled>1</NewEnabled>"
        << "<NewPortMappingDescription>torrentfs</NewPortMappingDescription>"
        << "<NewLeaseDuration>3600</NewLeaseDuration>"
        << "</u:AddPortMapping></s:Body></s:Envelope>";
    return out.str();
}

std::string PortMapper::buildUpnpDeletePortMappingSoap(std::uint16_t externalPort) const {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?>"
        << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        << "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        << "<s:Body><u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
        << "<NewRemoteHost></NewRemoteHost>"
        << "<NewExternalPort>" << externalPort << "</NewExternalPort>"
        << "<NewProtocol>TCP</NewProtocol>"
        << "</u:DeletePortMapping></s:Body></s:Envelope>";
    return out.str();
}

std::optional<PortMappingResult> PortMapper::parseNatPmpMapResponse(const std::vector<std::uint8_t>& packet) const {
    if (packet.size() < 16 || packet[1] != 130) {
        return std::nullopt;
    }
    PortMappingResult result;
    result.externalPort = static_cast<std::uint16_t>((packet[10] << 8) | packet[11]);
    result.lifetimeSeconds = (static_cast<std::uint32_t>(packet[12]) << 24) |
                             (static_cast<std::uint32_t>(packet[13]) << 16) |
                             (static_cast<std::uint32_t>(packet[14]) << 8) |
                             static_cast<std::uint32_t>(packet[15]);
    return result;
}

} // namespace vtfs
