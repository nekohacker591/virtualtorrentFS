#include "VirtualDrive.hpp"

#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#include <projectedfslib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#endif

namespace vtfs {

#ifdef _WIN32
namespace {

std::wstring decodeText(const std::string& value, UINT codePage) {
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(codePage, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(codePage, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

std::wstring torrentTextToWide(const std::string& value) {
    if (const auto utf8 = decodeText(value, CP_UTF8); !utf8.empty()) {
        return utf8;
    }
    if (const auto acp = decodeText(value, CP_ACP); !acp.empty()) {
        return acp;
    }
    return std::wstring(value.begin(), value.end());
}

std::wstring sanitizeWindowsComponent(std::wstring name) {
    static constexpr wchar_t replacement = L'_';
    static constexpr wchar_t invalidChars[] = L"<>:\"/\\|?*";
    for (wchar_t& ch : name) {
        if (ch < 32 || wcschr(invalidChars, ch) != nullptr) {
            ch = replacement;
        }
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) {
        name.back() = replacement;
    }
    if (name.empty()) {
        name = L"_";
    }
    return name;
}

std::wstring withLongPathPrefix(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) {
        return path;
    }
    if (path.rfind(L"\\\\", 0) == 0) {
        return L"\\\\?\\UNC\\" + path.substr(2);
    }
    return L"\\\\?\\" + path;
}

std::wstring toWidePath(const std::filesystem::path& path) {
    return withLongPathPrefix(path.wstring());
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

bool ensureDirectoryTree(const std::filesystem::path& path) {
    if (path.empty()) {
        return true;
    }

    std::filesystem::path current;
    if (path.has_root_name()) {
        current /= path.root_name();
    }
    if (path.has_root_directory()) {
        current /= path.root_directory();
    }

    for (const auto& part : path.relative_path()) {
        current /= part;
        const auto wide = toWidePath(current);
        if (CreateDirectoryW(wide.c_str(), nullptr) == 0) {
            const auto error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
    }
    return true;
}

std::wstring asciiFallbackComponent(const std::string& token) {
    std::wstring out;
    out.reserve(token.size() + 10);
    for (unsigned char ch : token) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '.' || ch == '-' || ch == '_' || ch == '(' || ch == ')' || ch == '[' || ch == ']') {
            out.push_back(static_cast<wchar_t>(ch));
        } else {
            out.push_back(L'_');
        }
    }
    if (out.empty()) {
        out = L"file";
    }
    std::uint32_t hash = 2166136261u;
    for (unsigned char ch : token) {
        hash ^= ch;
        hash *= 16777619u;
    }
    wchar_t suffix[10]{};
    swprintf_s(suffix, L"_%08X", hash);
    out += suffix;
    return sanitizeWindowsComponent(out);
}

std::filesystem::path materializedPath(const std::filesystem::path& root, const std::string& torrentRelativePath, bool forceAscii = false) {
    std::filesystem::path path = root;
    std::size_t start = 0;
    while (start < torrentRelativePath.size()) {
        const auto slash = torrentRelativePath.find('/', start);
        const auto token = torrentRelativePath.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        path /= (forceAscii ? asciiFallbackComponent(token) : sanitizeWindowsComponent(torrentTextToWide(token)));
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return path;
}

bool ensureGhostPlaceholderFile(const std::filesystem::path& path, std::uint64_t) {
    if (!ensureDirectoryTree(path.parent_path())) {
        return false;
    }

    const auto widePath = toWidePath(path);
    HANDLE file = CreateFileW(widePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    CloseHandle(file);
    SetFileAttributesW(widePath.c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_OFFLINE);
    return true;
}

bool linkOrPlaceholder(const std::filesystem::path& rootFile, const std::filesystem::path& cacheFile, std::uint64_t size) {
    std::error_code ec;
    std::filesystem::remove(rootFile, ec);

    if (!cacheFile.empty() && std::filesystem::exists(cacheFile)) {
        if (!ensureDirectoryTree(rootFile.parent_path())) {
            return false;
        }
        const auto rootWide = toWidePath(rootFile);
        const auto cacheWide = toWidePath(cacheFile);
        if (CreateHardLinkW(rootWide.c_str(), cacheWide.c_str(), nullptr) != FALSE) {
            SetFileAttributesW(rootWide.c_str(), FILE_ATTRIBUTE_READONLY);
            return true;
        }
    }

    return ensureGhostPlaceholderFile(rootFile, size);
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
        ensureDirectoryTree(root_);

        GUID instanceId{};
        if (CoCreateGuid(&instanceId) != S_OK) {
            std::cerr << "Failed to allocate provider GUID.\n";
            return 1;
        }

        HRESULT hr = PrjMarkDirectoryAsPlaceholder(root_.wstring().c_str(), nullptr, nullptr, &instanceId);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_REPARSE_TAG_MISMATCH)) {
            std::cerr << "PrjMarkDirectoryAsPlaceholder failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                      << ". Falling back to cache-aware sparse placeholders.\n";
            if (!materializeFallbackTree()) {
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
        options.PoolThreadCount = 8;
        options.ConcurrentThreadCount = 8;
        hr = PrjStartVirtualizing(root_.wstring().c_str(), &callbacks_, this, &options, &namespaceHandle_);
        if (FAILED(hr)) {
            std::cerr << "PrjStartVirtualizing failed: 0x" << std::hex << static_cast<unsigned long>(hr)
                      << ". Falling back to cache-aware sparse placeholders.\n";
            namespaceHandle_ = nullptr;
            if (!materializeFallbackTree()) {
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
                  << (projFsActive ? "Windows ProjFS" : "cache-aware sparse placeholder fallback") << ".\n"
                  << "Virtualization root: " << root_.string() << "\n"
                  << "Torrent: " << session_->metadata().name() << "\n"
                  << "Files: " << session_->metadata().files().size() << "\n"
                  << "Info hash: " << session_->metadata().infoHashHex() << "\n";
        return 0;
    }

    bool materializeFallbackTree() {
        const auto& files = session_->metadata().files();
        if (files.empty()) {
            return true;
        }

        std::atomic<std::size_t> index{0};
        std::atomic<bool> failed{false};
        std::mutex errorMutex;
        std::string firstError;

        const unsigned int threadCount = (std::max)(2u, std::thread::hardware_concurrency());
        std::vector<std::thread> workers;
        workers.reserve(threadCount);

        for (unsigned int t = 0; t < threadCount; ++t) {
            workers.emplace_back([&]() {
                while (!failed.load()) {
                    const auto i = index.fetch_add(1);
                    if (i >= files.size()) {
                        break;
                    }
                    const auto& record = files[i];
                    const auto entry = session_->lookup(record.relativePath);
                    const auto cacheFile = entry ? session_->payloadPathFor(*entry) : std::filesystem::path{};
                    const auto rootFile = materializedPath(root_, record.relativePath, false);
                    if (!linkOrPlaceholder(rootFile, cacheFile, record.size)) {
                        const auto asciiFallbackFile = materializedPath(root_, record.relativePath, true);
                        if (!linkOrPlaceholder(asciiFallbackFile, cacheFile, record.size)) {
                            failed.store(true);
                            std::scoped_lock lock(errorMutex);
                            if (firstError.empty()) {
                                firstError = rootFile.string();
                            }
                            break;
                        }
                    }
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        if (failed.load()) {
            std::cerr << "Failed to materialize fallback file: " << firstError << "\n";
            return false;
        }
        return true;
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
                    return PrjFileNameMatch(torrentTextToWide(leaf).c_str(), expr.c_str()) == FALSE;
                }), state.entries.end());
            }
            std::sort(state.entries.begin(), state.entries.end(), [](const FileEntry& a, const FileEntry& b) {
                const auto aLeafPos = a.virtualPath.find_last_of('/');
                const auto aLeaf = aLeafPos == std::string::npos ? a.virtualPath : a.virtualPath.substr(aLeafPos + 1);
                const auto bLeafPos = b.virtualPath.find_last_of('/');
                const auto bLeaf = bLeafPos == std::string::npos ? b.virtualPath : b.virtualPath.substr(bLeafPos + 1);
                return PrjFileNameCompare(torrentTextToWide(aLeaf).c_str(), torrentTextToWide(bLeaf).c_str()) < 0;
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
            const auto result = PrjFillDirEntryBuffer(torrentTextToWide(leaf).c_str(), &basicInfo, dirEntryBufferHandle);
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

        if (!entry->isDirectory) {
            provider->session_->beginStreaming(*entry);
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

        provider->session_->queueRead(*entry, byteOffset, length);
        void* buffer = PrjAllocateAlignedBuffer(provider->namespaceHandle_, length);
        if (buffer == nullptr) {
            return E_OUTOFMEMORY;
        }
        std::memset(buffer, 0, length);

        std::uint32_t bytesRead = 0;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const bool haveData = provider->session_->tryReadFileRange(*entry, byteOffset, buffer, length, bytesRead);
            if (haveData && bytesRead > 0) {
                const auto result = PrjWriteFileData(callbackData->NamespaceVirtualizationContext, &callbackData->DataStreamId,
                                                    buffer, byteOffset, bytesRead);
                PrjFreeAlignedBuffer(buffer);
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        PrjFreeAlignedBuffer(buffer);
        return HRESULT_FROM_WIN32(ERROR_FILE_OFFLINE);
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
