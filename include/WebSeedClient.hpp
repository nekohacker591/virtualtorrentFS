#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vtfs {

class WebSeedClient {
  public:
    explicit WebSeedClient(std::string baseUrl = {});

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] std::optional<std::string> buildFileUrl(const std::string& virtualPath) const;
    bool downloadFile(const std::string& virtualPath, const std::filesystem::path& destination) const;

  private:
    std::string baseUrl_;
};

} // namespace vtfs
