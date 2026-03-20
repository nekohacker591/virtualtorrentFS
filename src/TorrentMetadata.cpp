#include "TorrentMetadata.hpp"

#include "Bencode.hpp"
#include "Sha1.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace vtfs {

namespace {
BencodeValue requireKey(const BencodeDictionary& dict, const std::string& key) {
    const auto it = dict.find(key);
    if (it == dict.end()) {
        throw std::runtime_error("Missing torrent key: " + key);
    }
    return it->second;
}

std::string joinPathList(const BencodeList& pathParts) {
    std::string out;
    for (std::size_t i = 0; i < pathParts.size(); ++i) {
        if (!pathParts[i].isString()) {
            throw std::runtime_error("Torrent path component must be a string.");
        }
        if (!out.empty()) {
            out += '/';
        }
        out += pathParts[i].asString();
    }
    return out;
}
}

TorrentMetadata TorrentMetadata::loadFromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open torrent file: " + path.string());
    }
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto rootValue = BencodeParser(raw).parse();
    if (!rootValue.isDictionary()) {
        throw std::runtime_error("Torrent root must be a dictionary.");
    }

    const auto& root = rootValue.asDictionary();
    const auto& infoValue = requireKey(root, "info");
    if (!infoValue.isDictionary()) {
        throw std::runtime_error("Torrent info must be a dictionary.");
    }
    const auto& info = infoValue.asDictionary();

    TorrentMetadata metadata;
    metadata.name_ = requireKey(info, "name").asString();
    metadata.pieceLength_ = static_cast<std::uint64_t>(requireKey(info, "piece length").asInteger());
    const auto piecesRaw = requireKey(info, "pieces").asString();
    metadata.pieceCount_ = piecesRaw.size() / 20;
    metadata.infoHashHex_ = Sha1::toHex(Sha1::digest(encodeBencode(infoValue)));

    if (const auto filesIt = info.find("files"); filesIt != info.end()) {
        if (!filesIt->second.isList()) {
            throw std::runtime_error("Torrent files entry must be a list.");
        }
        std::uint64_t offset = 0;
        for (const auto& fileValue : filesIt->second.asList()) {
            if (!fileValue.isDictionary()) {
                throw std::runtime_error("Torrent file entry must be a dictionary.");
            }
            const auto& fileDict = fileValue.asDictionary();
            TorrentFileRecord record;
            record.relativePath = metadata.name_ + "/" + joinPathList(requireKey(fileDict, "path").asList());
            record.size = static_cast<std::uint64_t>(requireKey(fileDict, "length").asInteger());
            record.offset = offset;
            offset += record.size;
            metadata.totalSize_ += record.size;
            metadata.files_.push_back(std::move(record));
        }
    } else {
        TorrentFileRecord record;
        record.relativePath = metadata.name_;
        record.size = static_cast<std::uint64_t>(requireKey(info, "length").asInteger());
        record.offset = 0;
        metadata.totalSize_ = record.size;
        metadata.files_.push_back(std::move(record));
    }

    return metadata;
}

const std::string& TorrentMetadata::name() const { return name_; }
const std::string& TorrentMetadata::infoHashHex() const { return infoHashHex_; }
std::uint64_t TorrentMetadata::totalSize() const { return totalSize_; }
std::uint64_t TorrentMetadata::pieceLength() const { return pieceLength_; }
std::size_t TorrentMetadata::pieceCount() const { return pieceCount_; }
const std::vector<TorrentFileRecord>& TorrentMetadata::files() const { return files_; }

} // namespace vtfs
