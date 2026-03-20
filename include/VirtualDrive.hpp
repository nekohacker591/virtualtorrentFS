#pragma once

#include "Config.hpp"
#include "TorrentSession.hpp"

#include <memory>

namespace vtfs {

class VirtualDriveProvider {
  public:
    virtual ~VirtualDriveProvider() = default;
    virtual int mount(const Config& config, std::shared_ptr<TorrentSession> session) = 0;
};

class NullVirtualDriveProvider final : public VirtualDriveProvider {
  public:
    int mount(const Config& config, std::shared_ptr<TorrentSession> session) override;
};

class VirtualDrive {
  public:
    VirtualDrive(const Config& config, std::shared_ptr<TorrentSession> session);
    int mount();

  private:
    Config config_;
    std::shared_ptr<TorrentSession> session_;
    std::unique_ptr<VirtualDriveProvider> provider_;
};

} // namespace vtfs
