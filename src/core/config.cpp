#include "shared_km/core/config.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace shared_km::core {

namespace {

class SimpleJsonReader {
public:
    explicit SimpleJsonReader(std::string text) : text_(std::move(text)) {}

    std::unordered_map<std::string, std::string> Parse() {
        pos_ = 0;
        SkipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != '{') {
            return {};
        }
        pos_++; // skip '{'

        std::unordered_map<std::string, std::string> result;
        while (pos_ < text_.size()) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] == '}') {
                break;
            }

            const auto key = ReadString();
            if (!key) break;

            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') break;
            pos_++; // skip ':'

            SkipWhitespace();
            const auto value = ReadValue();
            if (!value) break;

            result[*key] = *value;

            SkipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                pos_++; // skip ','
            }
        }

        if (pos_ < text_.size() && text_[pos_] == '}') {
            pos_++; // skip '}'
        }

        return result;
    }

private:
    void SkipWhitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
               text_[pos_] == '\n' || text_[pos_] == '\r')) {
            pos_++;
        }
    }

    std::optional<std::string> ReadString() {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return std::nullopt;
        }
        pos_++; // skip opening quote

        std::string result;
        while (pos_ < text_.size()) {
            const auto c = text_[pos_];
            if (c == '"') {
                pos_++; // skip closing quote
                return result;
            }
            if (c == '\\' && pos_ + 1 < text_.size()) {
                pos_++;
                switch (text_[pos_]) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    std::string hex(text_.substr(pos_ + 1, 4));
                    result += "?";
                    pos_ += 4;
                    break;
                }
                default: result += text_[pos_]; break;
                }
                pos_++;
            } else {
                result += c;
                pos_++;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> ReadValue() {
        SkipWhitespace();
        if (pos_ >= text_.size()) return std::nullopt;

        const auto c = text_[pos_];
        if (c == '"') {
            return ReadString();
        }
        if (c == 't' && text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return "true";
        }
        if (c == 'f' && text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return "false";
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            const auto start = pos_;
            if (text_[pos_] == '-') pos_++;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                pos_++;
            }
            return std::string(text_.substr(start, pos_ - start));
        }
        return std::nullopt;
    }

    std::string text_;
    std::size_t pos_ = 0;
};

}  // namespace

RuntimeConfig LoadConfigOrDefaults(const std::filesystem::path& path) {
    RuntimeConfig config;
    config.config_path = path;
    if (path.filename() == "sender.json") {
        config.host = "127.0.0.1";
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();

    if (text.empty()) {
        return config;
    }

    SimpleJsonReader reader(text);
    const auto values = reader.Parse();
    if (values.empty()) {
        return config;
    }

    auto it = values.find("token");
    if (it != values.end()) {
        config.token = it->second;
    }

    it = values.find("host");
    if (it != values.end()) {
        config.host = it->second;
    }

    it = values.find("port");
    if (it != values.end()) {
        config.port = static_cast<std::uint16_t>(std::stoi(it->second));
    }

    it = values.find("edge_switching_enabled");
    if (it != values.end()) {
        config.edge_switching_enabled = it->second == "true";
    }

    return config;
}

}  // namespace shared_km::core
