#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cryptoprice::json
{
struct Value
{
    std::string scalar;
    std::map<std::string, Value> object;
    std::vector<Value> array;
    char kind{};
    const Value& Get(const std::string& key) const
    {
        const auto found = object.find(key);
        if (kind != '{' || found == object.end()) throw std::runtime_error("Missing field: " + key);
        return found->second;
    }
    std::string Text() const
    {
        if (kind != 's' && kind != 'n') return "";
        if (kind != 'n' || scalar.find_first_of("eE") == std::string::npos) return scalar;
        // Keep numeric Kraken prices compatible with decimal precision inference.
        const double value = std::strtod(scalar.c_str(), nullptr);
        if (!std::isfinite(value)) return "";
        std::ostringstream output;
        output << std::fixed << std::setprecision(10) << value;
        std::string result = output.str();
        while (result.size() > 1 && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
        return result;
    }
    std::string Text(const std::string& key) const
    {
        const auto found = object.find(key);
        return found == object.end() ? "" : found->second.Text();
    }
};
class Parser
{
public:
    explicit Parser(const std::string& text) : text_(text) {}
    Value Parse()
    {
        Value value = Read(0);
        Space();
        if (pos_ != text_.size()) throw std::runtime_error("Trailing JSON data");
        return value;
    }
private:
    void Space() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    char Take() { if (pos_ == text_.size()) throw std::runtime_error("Truncated JSON"); return text_[pos_++]; }
    void Expect(char ch) { Space(); if (Take() != ch) throw std::runtime_error("Invalid JSON delimiter"); }
    std::string String()
    {
        std::string result;
        while (true)
        {
            char ch = Take();
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 32) throw std::runtime_error("Invalid JSON string");
            if (ch == '\\')
            {
                ch = Take();
                if (ch == 'u')
                {
                    for (int i = 0; i < 4; ++i)
                        if (!std::isxdigit(static_cast<unsigned char>(Take()))) throw std::runtime_error("Invalid Unicode escape");
                    result += '?'; // Exchange identifiers and price fields are ASCII.
                    continue;
                }
                if (ch == 'n') ch = '\n';
                else if (ch == 'r') ch = '\r';
                else if (ch == 't') ch = '\t';
                else if (ch == 'b') ch = '\b';
                else if (ch == 'f') ch = '\f';
                else if (ch != '"' && ch != '\\' && ch != '/') throw std::runtime_error("Invalid escape");
            }
            result += ch;
        }
    }
    Value Read(int depth)
    {
        if (depth > 32) throw std::runtime_error("JSON nesting too deep");
        Space();
        const char ch = Take();
        Value value;
        value.kind = ch;
        if (ch == '"') { value.kind = 's'; value.scalar = String(); return value; }
        if (ch == '{' || ch == '[')
        {
            const char end = ch == '{' ? '}' : ']';
            Space();
            if (pos_ < text_.size() && text_[pos_] == end) { ++pos_; return value; }
            while (true)
            {
                if (ch == '{')
                {
                    Expect('"');
                    const std::string key = String();
                    Expect(':');
                    if (!value.object.emplace(key, Read(depth + 1)).second) throw std::runtime_error("Duplicate JSON key");
                }
                else value.array.push_back(Read(depth + 1));
                Space();
                const char separator = Take();
                if (separator == end) return value;
                if (separator != ',') throw std::runtime_error("Invalid JSON separator");
            }
        }
        const std::size_t start = pos_ - 1;
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']' &&
               !std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        value.scalar = text_.substr(start, pos_ - start);
        if (value.scalar == "true" || value.scalar == "false" || value.scalar == "null")
        { value.kind = 'l'; return value; }
        // Enforce JSON number grammar, including exponent notation.
        std::size_t i = value.scalar[0] == '-' ? 1 : 0;
        const auto digits = [&] { const std::size_t begin = i; while (i < value.scalar.size() && std::isdigit(static_cast<unsigned char>(value.scalar[i]))) ++i; return i > begin; };
        if (i == value.scalar.size()) throw std::runtime_error("Invalid JSON number");
        if (value.scalar[i] == '0') ++i;
        else if (!digits()) throw std::runtime_error("Invalid JSON number");
        if (i < value.scalar.size() && value.scalar[i] == '.') { ++i; if (!digits()) throw std::runtime_error("Invalid fraction"); }
        if (i < value.scalar.size() && (value.scalar[i] == 'e' || value.scalar[i] == 'E'))
        { ++i; if (i < value.scalar.size() && (value.scalar[i] == '+' || value.scalar[i] == '-')) ++i; if (!digits()) throw std::runtime_error("Invalid exponent"); }
        if (i != value.scalar.size()) throw std::runtime_error("Invalid JSON scalar");
        value.kind = 'n';
        return value;
    }
    const std::string& text_;
    std::size_t pos_{};
};
} // namespace cryptoprice::json
