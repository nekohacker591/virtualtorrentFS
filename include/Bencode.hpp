#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace vtfs {

struct BencodeValue;
using BencodeInteger = std::int64_t;
using BencodeString = std::string;
using BencodeList = std::vector<BencodeValue>;
using BencodeDictionary = std::map<std::string, BencodeValue>;

struct BencodeValue : std::variant<BencodeInteger, BencodeString, BencodeList, BencodeDictionary> {
    using variant::variant;

    [[nodiscard]] bool isInteger() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isList() const;
    [[nodiscard]] bool isDictionary() const;

    [[nodiscard]] const BencodeInteger& asInteger() const;
    [[nodiscard]] const BencodeString& asString() const;
    [[nodiscard]] const BencodeList& asList() const;
    [[nodiscard]] const BencodeDictionary& asDictionary() const;
};

class BencodeParser {
  public:
    explicit BencodeParser(std::string_view input);
    [[nodiscard]] BencodeValue parse();

  private:
    [[nodiscard]] BencodeValue parseValue();
    [[nodiscard]] BencodeInteger parseInteger();
    [[nodiscard]] BencodeString parseString();
    [[nodiscard]] BencodeList parseList();
    [[nodiscard]] BencodeDictionary parseDictionary();
    [[nodiscard]] char peek() const;
    char get();
    [[nodiscard]] bool eof() const;

    std::string_view input_;
    std::size_t pos_ = 0;
};

std::string encodeBencode(const BencodeValue& value);

} // namespace vtfs
