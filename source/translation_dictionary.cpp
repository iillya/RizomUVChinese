#include "rizomuv_localizer/translation_dictionary.h"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace rizomuv::localizer {
namespace {

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                          static_cast<int>(input.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring output(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), output.data(), count) != count)
        return {};
    return output;
}

class JsonReader {
public:
    explicit JsonReader(const std::wstring& source) : source_(source) {}

    bool ReadTranslations(std::unordered_map<std::wstring, std::wstring>& output,
                          std::wstring& error) {
        SkipWhitespace();
        if (!Consume(L'{')) return Fail(L"词库根节点必须是对象", error);
        while (true) {
            SkipWhitespace();
            if (Consume(L'}')) break;
            std::wstring key;
            if (!ReadString(key)) return Fail(L"无法读取词库字段名", error);
            SkipWhitespace();
            if (!Consume(L':')) return Fail(L"词库字段缺少冒号", error);
            SkipWhitespace();
            if (key == L"translations") {
                if (!ReadStringMap(output)) return Fail(L"translations 必须是字符串映射", error);
            } else if (!SkipValue()) {
                return Fail(L"词库包含无法解析的字段", error);
            }
            SkipWhitespace();
            if (Consume(L'}')) break;
            if (!Consume(L',')) return Fail(L"词库字段之间缺少逗号", error);
        }
        SkipWhitespace();
        if (position_ != source_.size()) return Fail(L"词库根节点后包含多余数据", error);
        if (output.empty()) return Fail(L"词库没有有效翻译", error);
        return true;
    }

private:
    void SkipWhitespace() {
        while (position_ < source_.size() && iswspace(source_[position_])) ++position_;
    }
    bool Consume(wchar_t expected) {
        if (position_ >= source_.size() || source_[position_] != expected) return false;
        ++position_;
        return true;
    }
    bool ReadString(std::wstring& output) {
        if (!Consume(L'"')) return false;
        output.clear();
        while (position_ < source_.size()) {
            wchar_t ch = source_[position_++];
            if (ch == L'"') return true;
            if (ch < 0x20) return false;
            if (ch != L'\\') { output += ch; continue; }
            if (position_ >= source_.size()) return false;
            ch = source_[position_++];
            switch (ch) {
            case L'"': output += L'"'; break;
            case L'\\': output += L'\\'; break;
            case L'/': output += L'/'; break;
            case L'b': output += L'\b'; break;
            case L'f': output += L'\f'; break;
            case L'n': output += L'\n'; break;
            case L'r': output += L'\r'; break;
            case L't': output += L'\t'; break;
            case L'u': {
                if (position_ + 4 > source_.size()) return false;
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const wchar_t digit = source_[position_++];
                    value <<= 4;
                    if (digit >= L'0' && digit <= L'9') value += digit - L'0';
                    else if (digit >= L'a' && digit <= L'f') value += digit - L'a' + 10;
                    else if (digit >= L'A' && digit <= L'F') value += digit - L'A' + 10;
                    else return false;
                }
                output += static_cast<wchar_t>(value);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool ReadStringMap(std::unordered_map<std::wstring, std::wstring>& output) {
        if (!Consume(L'{')) return false;
        while (true) {
            SkipWhitespace();
            if (Consume(L'}')) return true;
            std::wstring source;
            std::wstring translated;
            if (!ReadString(source)) return false;
            SkipWhitespace();
            if (!Consume(L':')) return false;
            SkipWhitespace();
            if (!ReadString(translated)) return false;
            if (!source.empty() && !translated.empty()) output[source] = translated;
            SkipWhitespace();
            if (Consume(L'}')) return true;
            if (!Consume(L',')) return false;
        }
    }
    bool SkipValue() {
        SkipWhitespace();
        if (position_ >= source_.size()) return false;
        if (source_[position_] == L'"') { std::wstring ignored; return ReadString(ignored); }
        if (source_[position_] == L'{' || source_[position_] == L'[') {
            const wchar_t open = source_[position_++];
            const wchar_t close = open == L'{' ? L'}' : L']';
            int depth = 1;
            bool quoted = false;
            bool escaped = false;
            while (position_ < source_.size() && depth > 0) {
                const wchar_t ch = source_[position_++];
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (ch == L'\\') escaped = true;
                    else if (ch == L'"') quoted = false;
                } else if (ch == L'"') quoted = true;
                else if (ch == open) ++depth;
                else if (ch == close) --depth;
            }
            return depth == 0;
        }
        while (position_ < source_.size() && source_[position_] != L',' && source_[position_] != L'}')
            ++position_;
        return true;
    }
    bool Fail(const wchar_t* message, std::wstring& error) {
        error = std::wstring(message) + L"，位置 " + std::to_wstring(position_);
        return false;
    }

    const std::wstring& source_;
    size_t position_{};
};

} // namespace

bool TranslationDictionary::Load(const std::filesystem::path& path, std::wstring& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = L"无法打开词库：" + path.wstring(); return false; }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
        bytes.erase(0, 3);
    const std::wstring json = Utf8ToWide(bytes);
    if (json.empty()) { error = L"词库不是有效的 UTF-8 文件"; return false; }
    std::unordered_map<std::wstring, std::wstring> parsed;
    JsonReader reader(json);
    if (!reader.ReadTranslations(parsed, error)) return false;
    translations_ = std::move(parsed);
    return true;
}

const std::wstring* TranslationDictionary::Find(const std::wstring& source) const {
    const auto found = translations_.find(source);
    return found == translations_.end() ? nullptr : &found->second;
}

} // namespace rizomuv::localizer
