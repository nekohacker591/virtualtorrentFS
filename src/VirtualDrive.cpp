#include "VirtualDrive.hpp"

#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#include <projectedfslib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#endif

namespace vtfs {

#ifdef _WIN32
namespace {

std::wstring toWide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::string fromWindowsPath(PCWSTR filePathName) {
    if (filePathName == nullptr || *filePathName == L'\0') {
        return "/";
    }
    std::wstring value(filePathName);
    std::string out = "/";
    for (wchar_t ch : value) {
        out.push_back(ch == L'\\' ? '/' : static_cast<char>(ch));
    }
    return out;
}

std::wstring makeDosTarget(const std::filesystem::path& path) {
    return L"\\??\\" + path.wstring();
}

bool ensureSparsePlaceholderFile(const std::filesystem::path& path, std::uint64_t size) {
    std::filesystem::create_directories(path.parent_path());

    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);

    LARGE_INTEGER targetSize{};
    targetSize.QuadPart = static_cast<LONGLONG>(size);
    const BOOL pointerOk = SetFilePointerEx(file, targetSize, nullptr, FILE_BEGIN);
    const BOOL eofOk = pointerOk ? SetEndOfFile(file) : FALSE;
    CloseHandle(file);

    if (!pointerOk || !eofOk) {
        return false;
    }

    SetFileAttributesW(path.wstring().c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SPARSE_FILE);
    return true;
}

std::string guidKey(const GUID& guid) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(hex[(bytes[i] >> 4) & 0xF]);
        out.push_back(hex[bytes[i] & 0xF]);
    }
    return out;
}

class WindowsProjFsProvider final : public VirtualDriveProvider {
  public:
    int mount(const Config& config, const TorrentSession& session) override {
        session_ = &session;
        root_ = config.cacheDirectory / ".projfs-root";
        std::filesystem::create_directories(root_);

        GUID instanceId{};
        if (CoCreateGuid(&instanceId) != S_OK) {
            std::cerr << "Failed to allocate provider GUID.\n";
            return 1;
        }

        HRESULT hr = PrjMarkDirectoryAsPlaceholder(root_.wstring().c_str(), nullptr, nullptr, &instanceId);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_REPARSE_TAG_MISMATCH)) {
            std::cerr << "PrjMarkDirectoryAsPlaceholder failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                      << ". Falling back to sparse on-disk placeholders.\n";
            if (!materializeSparseFallbackTree()) {
                return 1;
            }
            return mapDriveLetter(config, false);
        }

        std::memset(&callbacks_, 0, sizeof(callbacks_));
        callbacks_.StartDirectoryEnumerationCallback = &WindowsProjFsProvider::startDirEnum;
        callbacks_.EndDirectoryEnumerationCallback = &WindowsProjFsProvider::endDirEnum;
        callbacks_.GetDirectoryEnumerationCallback = &WindowsProjFsProvider::getDirEnum;
        callbacks_.GetPlaceholderInfoCallback = &WindowsProjFsProvider::getPlaceholderInfo;
        callbacks_.GetFileDataCallback = &WindowsProjFsProvider::getFileData;

        PRJ_STARTVIRTUALIZING_OPTIONS options{};
        options.PoolThreadCount = 4;
        options.ConcurrentThreadCount = 4;
        hr = PrjStartVirtualizing(root_.wstring().c_str(), &callbacks_, this, &options, &namespaceHandle_);
        if (FAILED(hr)) {
            std::cerr << "PrjStartVirtualizing failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                      << ". Falling back to sparse on-disk placeholders.\n";
            namespaceHandle_ = nullptr;
            if (!materializeSparseFallbackTree()) {
                return 1;
            }
            return mapDriveLetter(config, false);
        }

        return mapDriveLetter(config, true);
    }

    ~WindowsProjFsProvider() override {
        if (namespaceHandle_ != nullptr) {
            PrjStopVirtualizing(namespaceHandle_);
        }
    }

  private:
    struct EnumerationState {
        std::vector<FileEntry> entries;
        std::size_t nextIndex = 0;
    };

    int mapDriveLetter(const Config& config, bool projFsActive) {
        const std::wstring deviceName = std::wstring(1, static_cast<wchar_t>(config.driveLetter)) + L":";
        if (!DefineDosDeviceW(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, deviceName.c_str(), makeDosTarget(root_).c_str())) {
            std::cerr << "DefineDosDeviceW failed for drive mapping.\n";
            if (namespaceHandle_ != nullptr) {
                PrjStopVirtualizing(namespaceHandle_);
                namespaceHandle_ = nullptr;
            }
            return 1;
        }

        std::cout << "Mounted namespace at " << std::string(1, config.driveLetter) << ":\\ using "
                  << (projFsActive ? "Windows ProjFS" : "sparse placeholder fallback") << ".\n"
                  << "Virtualization root: " << root_.string() << "\n"
                  << "Torrent: " << session_->metadata().name() << "\n"
                  << "Files: " << session_->metadata().files().size() << "\n"
                  << "Info hash: " << session_->metadata().infoHashHex() << "\n";
        return 0;
    }

    bool materializeSparseFallbackTree() {
        try {
            for (const auto& record : session_->metadata().files()) {
                std::filesystem::path path = root_;
                std::wstring relative = toWide(record.relativePath);
                for (wchar_t& ch : relative) {
                    if (ch == L'/') {
                        ch = L'\\';
                    }
                }
                path /= relative;
                if (!ensureSparsePlaceholderFile(path, record.size)) {
                    std::cerr << "Failed to create sparse placeholder: " << path.string() << "\n";
                    return false;
                }
            }
            return true;
        } catch (const std::exception& ex) {
            std::cerr << "Sparse fallback materialization failed: " << ex.what() << "\n";
            return false;
        }
    }

    static WindowsProjFsProvider* self(const PRJ_CALLBACK_DATA* callbackData) {
        return static_cast<WindowsProjFsProvider*>(callbackData->InstanceContext);
    }

    static HRESULT CALLBACK startDirEnum(const PRJ_CALLBACK_DATA* callbackData, const GUID* enumerationId) {
        auto* provider = self(callbackData);
        std::scoped_lock lock(provider->mutex_);
        provider->enumerations_[guidKey(*enumerationId)] = EnumerationState{};
        return S_OK;
    }

    static HRESULT CALLBACK endDirEnum(const PRJ_CALLBACK_DATA* callbackData, const GUID* enumerationId) {
        auto* provider = self(callbackData);
        std::scoped_lock lock(provider->mutex_);
        provider->enumerations_.erase(guidKey(*enumerationId));
        return S_OK;
    }

    static HRESULT CALLBACK getDirEnum(const PRJ_CALLBACK_DATA* callbackData,
                                       const GUID* enumerationId,
                                       PCWSTR searchExpression,
                                       PRJ_DIR_ENTRY_BUFFER_HANDLE dirEntryBufferHandle) {
        auto* provider = self(callbackData);
        const auto key = guidKey(*enumerationId);
        std::scoped_lock lock(provider->mutex_);
        auto& state = provider->enumerations_[key];

        if (callbackData->Flags & PRJ_CB_DATA_FLAG_ENUM_RESTART_SCAN || state.entries.empty()) {
            state.entries = provider->session_->listDirectory(fromWindowsPath(callbackData->FilePathName));
            if (searchExpression != nullptr && *searchExpression != L'\0') {
                const std::wstring expr(searchExpression);
                state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(), [&](const FileEntry& entry) {
                    const auto leafPos = entry.virtualPath.find_last_of('/');
                    const auto leaf = leafPos == std::string::npos ? entry.virtualPath : entry.virtualPath.substr(leafPos + 1);
                    return PrjFileNameMatch(toWide(leaf).c_str(), expr.c_str()) == FALSE;
                }), state.entries.end());
            }
            std::sort(state.entries.begin(), state.entries.end(), [](const FileEntry& a, const FileEntry& b) {
                const auto aLeafPos = a.virtualPath.find_last_of('/');
                const auto aLeaf = aLeafPos == std::string::npos ? a.virtualPath : a.virtualPath.substr(aLeafPos + 1);
                const auto bLeafPos = b.virtualPath.find_last_of('/');
                const auto bLeaf = bLeafPos == std::string::npos ? b.virtualPath : b.virtualPath.substr(bLeafPos + 1);
                return PrjFileNameCompare(toWide(aLeaf).c_str(), toWide(bLeaf).c_str()) < 0;
            });
            state.nextIndex = 0;
        }

        while (state.nextIndex < state.entries.size()) {
            const auto& entry = state.entries[state.nextIndex];
            PRJ_FILE_BASIC_INFO basicInfo{};
            basicInfo.IsDirectory = entry.isDirectory;
            basicInfo.FileSize = entry.size;
            const auto leafPos = entry.virtualPath.find_last_of('/');
            const auto leaf = leafPos == std::string::npos ? entry.virtualPath : entry.virtualPath.substr(leafPos + 1);
            const auto result = PrjFillDirEntryBuffer(toWide(leaf).c_str(), &basicInfo, dirEntryBufferHandle);
            if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) {
                return S_OK;
            }
            if (FAILED(result)) {
                return result;
            }
            ++state.nextIndex;
        }
        return S_OK;
    }

    static HRESULT CALLBACK getPlaceholderInfo(const PRJ_CALLBACK_DATA* callbackData) {
        auto* provider = self(callbackData);
        const auto entry = provider->session_->lookup(fromWindowsPath(callbackData->FilePathName));
        if (!entry) {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        PRJ_PLACEHOLDER_INFO info{};
        info.FileBasicInfo.IsDirectory = entry->isDirectory;
        info.FileBasicInfo.FileSize = entry->size;
        return PrjWritePlaceholderInfo(provider->namespaceHandle_, callbackData->FilePathName, &info, sizeof(info));
    }

    static HRESULT CALLBACK getFileData(const PRJ_CALLBACK_DATA* callbackData, std::uint64_t byteOffset, std::uint32_t length) {
        auto* provider = self(callbackData);
        const auto entry = provider->session_->lookup(fromWindowsPath(callbackData->FilePathName));
        if (!entry || entry->isDirectory) {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        provider->session_->beginStreaming(*entry);
        void* buffer = PrjAllocateAlignedBuffer(provider->namespaceHandle_, length);
        if (buffer == nullptr) {
            return E_OUTOFMEMORY;
        }
        std::memset(buffer, 0, length);

        std::uint32_t bytesRead = 0;
        const bool haveData = provider->session_->tryReadFileRange(*entry, byteOffset, buffer, length, bytesRead);
        if (!haveData) {
            PrjFreeAlignedBuffer(buffer);
            return HRESULT_FROM_WIN32(ERROR_FILE_OFFLINE);
        }

        const auto result = PrjWriteFileData(callbackData->NamespaceVirtualizationContext, &callbackData->DataStreamId,
                                            buffer, byteOffset, bytesRead);
        PrjFreeAlignedBuffer(buffer);
        return result;
    }

    const TorrentSession* session_ = nullptr;
    std::filesystem::path root_;
    PRJ_NAMESPACE_VIRTUALIZATION_CONTEXT namespaceHandle_ = nullptr;
    PRJ_CALLBACKS callbacks_{};
    std::mutex mutex_;
    std::unordered_map<std::string, EnumerationState> enumerations_;
};

} // namespace
#endif

int NullVirtualDriveProvider::mount(const Config& config, const TorrentSession& session) {
    std::cout
        << "VirtualTorrentFS loaded torrent metadata successfully.\n"
        << "Drive letter requested: " << config.driveLetter << ":\\\n"
        << "Torrent name: " << session.metadata().name() << "\n"
        << "Info hash: " << session.metadata().infoHashHex() << "\n"
        << "Logical size: " << session.totalSize() << " bytes\n"
        << "Files exposed: " << session.metadata().files().size() << "\n\n"
        << "This build can parse torrents and build the virtual namespace,\n"
        << "but the active OS build does not have the Windows mount path enabled.\n";
    return 2;
}

VirtualDrive::VirtualDrive(const Config& config, std::shared_ptr<TorrentSession> session)
    : config_(config),
      session_(std::move(session)) {
#ifdef _WIN32
    provider_ = std::make_unique<WindowsProjFsProvider>();
#else
    provider_ = std::make_unique<NullVirtualDriveProvider>();
#endif
}

int VirtualDrive::mount() {
    return provider_->mount(config_, *session_);
}

} // namespace vtfs
