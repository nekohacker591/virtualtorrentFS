#include "Bencode.hpp"

#include <charconv>
#include <sstream>

namespace vtfs {

bool BencodeValue::isInteger() const { return std::holds_alternative<BencodeInteger>(*this); }
bool BencodeValue::isString() const { return std::holds_alternative<BencodeString>(*this); }
bool BencodeValue::isList() const { return std::holds_alternative<BencodeList>(*this); }
bool BencodeValue::isDictionary() const { return std::holds_alternative<BencodeDictionary>(*this); }
const BencodeInteger& BencodeValue::asInteger() const { return std::get<BencodeInteger>(*this); }
const BencodeString& BencodeValue::asString() const { return std::get<BencodeString>(*this); }
const BencodeList& BencodeValue::asList() const { return std::get<BencodeList>(*this); }
const BencodeDictionary& BencodeValue::asDictionary() const { return std::get<BencodeDictionary>(*this); }

BencodeParser::BencodeParser(std::string_view input) : input_(input) {}

BencodeValue BencodeParser::parse() {
    const auto value = parseValue();
    if (!eof()) {
        throw std::runtime_error("Trailing bytes after bencode root value.");
    }
    return value;
}

BencodeValue BencodeParser::parseValue() {
    if (eof()) {
        throw std::runtime_error("Unexpected end of input.");
    }

    const char ch = peek();
    if (ch == 'i') {
        return parseInteger();
    }
    if (ch == 'l') {
        return parseList();
    }
    if (ch == 'd') {
        return parseDictionary();
    }
    if (ch >= '0' && ch <= '9') {
        return parseString();
    }
    throw std::runtime_error("Invalid bencode token.");
}

BencodeInteger BencodeParser::parseInteger() {
    get();
    const std::size_t start = pos_;
    while (!eof() && peek() != 'e') {
        ++pos_;
    }
    if (eof()) {
        throw std::runtime_error("Unterminated integer.");
    }

    const auto token = input_.substr(start, pos_ - start);
    get();

    std::int64_t value = 0;
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("Invalid integer value.");
    }
    return value;
}

BencodeString BencodeParser::parseString() {
    const std::size_t lenStart = pos_;
    while (!eof() && peek() != ':') {
        ++pos_;
    }
    if (eof()) {
        throw std::runtime_error("Invalid string length header.");
    }

    std::size_t length = 0;
    const auto token = input_.substr(lenStart, pos_ - lenStart);
    const auto* begin = token.data();
    const auto* end = token.data() + token.size();
    const auto result = std::from_chars(begin, end, length);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::runtime_error("Invalid string length.");
    }

    get();
    if (pos_ + length > input_.size()) {
        throw std::runtime_error("String exceeds input size.");
    }

    std::string output(input_.substr(pos_, length));
    pos_ += length;
    return output;
}

BencodeList BencodeParser::parseList() {
    get();
    BencodeList list;
    while (!eof() && peek() != 'e') {
        list.push_back(parseValue());
    }
    if (eof()) {
        throw std::runtime_error("Unterminated list.");
    }
    get();
    return list;
}

BencodeDictionary BencodeParser::parseDictionary() {
    get();
    BencodeDictionary dictionary;
    while (!eof() && peek() != 'e') {
        const auto key = parseString();
        dictionary.emplace(key, parseValue());
    }
    if (eof()) {
        throw std::runtime_error("Unterminated dictionary.");
    }
    get();
    return dictionary;
}

char BencodeParser::peek() const {
    return input_[pos_];
}

char BencodeParser::get() {
    return input_[pos_++];
}

bool BencodeParser::eof() const {
    return pos_ >= input_.size();
}

std::string encodeBencode(const BencodeValue& value) {
    if (value.isInteger()) {
        return "i" + std::to_string(value.asInteger()) + "e";
    }
    if (value.isString()) {
        return std::to_string(value.asString().size()) + ":" + value.asString();
    }
    if (value.isList()) {
        std::string out = "l";
        for (const auto& item : value.asList()) {
            out += encodeBencode(item);
        }
        out += 'e';
        return out;
    }

    std::string out = "d";
    for (const auto& [key, child] : value.asDictionary()) {
        out += std::to_string(key.size()) + ":" + key;
        out += encodeBencode(child);
    }
    out += 'e';
    return out;
}

} // namespace vtfs
