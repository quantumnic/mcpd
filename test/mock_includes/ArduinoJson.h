/**
 * Self-contained ArduinoJson v7 mock for native test builds.
 * Provides minimal stubs for JsonDocument, JsonObject, JsonArray, JsonVariant,
 * serializeJson, deserializeJson, etc.
 */
#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// Forward declarations
class JsonVariant;
class JsonObject;
class JsonArray;
class JsonDocument;

// ── JSON Value Node ────────────────────────────────────────────────────

enum class JsonNodeType { Null, Bool, Int, Float, String, Object, Array };

struct JsonNode {
    JsonNodeType type = JsonNodeType::Null;
    bool boolVal = false;
    long longVal = 0;
    double doubleVal = 0.0;
    std::string strVal;
    std::vector<std::pair<std::string, std::shared_ptr<JsonNode>>> objectVals;
    std::vector<std::shared_ptr<JsonNode>> arrayVals;

    std::shared_ptr<JsonNode>& objGet(const std::string& key) {
        for (auto& p : objectVals) {
            if (p.first == key) return p.second;
        }
        objectVals.push_back({key, std::make_shared<JsonNode>()});
        return objectVals.back().second;
    }

    bool objHas(const std::string& key) const {
        for (auto& p : objectVals) {
            if (p.first == key) return true;
        }
        return false;
    }

    size_t size() const {
        if (type == JsonNodeType::Array) return arrayVals.size();
        if (type == JsonNodeType::Object) return objectVals.size();
        return 0;
    }
};

// ── JsonVariant ────────────────────────────────────────────────────────

class JsonVariant {
public:
    std::shared_ptr<JsonNode> _node;

    JsonVariant() : _node(std::make_shared<JsonNode>()) {}
    JsonVariant(std::shared_ptr<JsonNode> n) : _node(n ? n : std::make_shared<JsonNode>()) {}

    bool isNull() const { return !_node || _node->type == JsonNodeType::Null; }

    // Implicit conversions
    // Implicit conversions (ArduinoJson v7 compatible)
    operator const char*() const {
        if (!_node || _node->type != JsonNodeType::String) return nullptr;
        return _node->strVal.c_str();
    }
    operator int() const {
        if (!_node) return 0;
        if (_node->type == JsonNodeType::Int) return (int)_node->longVal;
        if (_node->type == JsonNodeType::Float) return (int)_node->doubleVal;
        return 0;
    }
    operator float() const {
        if (!_node) return 0;
        if (_node->type == JsonNodeType::Float) return (float)_node->doubleVal;
        if (_node->type == JsonNodeType::Int) return (float)_node->longVal;
        return 0;
    }
    operator double() const {
        if (!_node) return 0;
        if (_node->type == JsonNodeType::Float) return _node->doubleVal;
        if (_node->type == JsonNodeType::Int) return (double)_node->longVal;
        return 0;
    }
    operator long() const {
        if (!_node) return 0;
        if (_node->type == JsonNodeType::Int) return _node->longVal;
        return 0;
    }
    operator unsigned long() const { return (unsigned long)(long)*this; }
    operator JsonObject() const;
    operator JsonArray() const;

    // operator[] for object access
    JsonVariant operator[](const char* key) const {
        if (!_node || (_node->type != JsonNodeType::Object && _node->type != JsonNodeType::Null))
            return JsonVariant();
        if (_node->type == JsonNodeType::Null) {
            _node->type = JsonNodeType::Object;
        }
        return JsonVariant(_node->objGet(key));
    }

    JsonVariant operator[](const std::string& key) const { return operator[](key.c_str()); }

    // operator[] for array access
    JsonVariant operator[](int index) const {
        if (!_node || _node->type != JsonNodeType::Array) return JsonVariant();
        if (index < 0 || (size_t)index >= _node->arrayVals.size()) return JsonVariant();
        return JsonVariant(_node->arrayVals[index]);
    }
    JsonVariant operator[](size_t index) const { return operator[]((int)index); }

    // Assignment operators
    JsonVariant& operator=(const char* s) {
        if (_node) { _node->type = JsonNodeType::String; _node->strVal = s ? s : ""; }
        return *this;
    }
    JsonVariant& operator=(const std::string& s) { return operator=(s.c_str()); }
    JsonVariant& operator=(bool b) {
        if (_node) { _node->type = JsonNodeType::Bool; _node->boolVal = b; }
        return *this;
    }
    JsonVariant& operator=(int v) {
        if (_node) { _node->type = JsonNodeType::Int; _node->longVal = v; }
        return *this;
    }
    JsonVariant& operator=(long v) {
        if (_node) { _node->type = JsonNodeType::Int; _node->longVal = v; }
        return *this;
    }
    JsonVariant& operator=(unsigned int v) {
        if (_node) { _node->type = JsonNodeType::Int; _node->longVal = (long)v; }
        return *this;
    }
    JsonVariant& operator=(unsigned long v) {
        if (_node) { _node->type = JsonNodeType::Int; _node->longVal = (long)v; }
        return *this;
    }
    JsonVariant& operator=(double v) {
        if (_node) { _node->type = JsonNodeType::Float; _node->doubleVal = v; }
        return *this;
    }
    JsonVariant& operator=(float v) { return operator=((double)v); }
    JsonVariant& operator=(std::nullptr_t) {
        if (_node) { _node->type = JsonNodeType::Null; }
        return *this;
    }
    // Deep-copy from another variant
    JsonVariant& operator=(const JsonVariant& other) {
        if (_node && other._node && _node != other._node) {
            *_node = *other._node;
        }
        return *this;
    }

    // as<T> conversions
    template<typename T> T as() const;

    // is<T> checks
    template<typename T> bool is() const;

    bool containsKey(const char* key) const {
        return _node && _node->type == JsonNodeType::Object && _node->objHas(key);
    }
    bool containsKey(const std::string& key) const { return containsKey(key.c_str()); }

    size_t size() const { return _node ? _node->size() : 0; }

    // to<T> - convert this node to the given type
    template<typename T> T to();

    // operator| for default values (ArduinoJson's "or" operator)
    const char* operator|(const char* defaultVal) const {
        if (isNull() || _node->type != JsonNodeType::String) return defaultVal;
        return _node->strVal.c_str();
    }
    bool operator|(bool defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Bool) return _node->boolVal;
        return defaultVal;
    }
    int operator|(int defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Int) return (int)_node->longVal;
        if (_node->type == JsonNodeType::Float) return (int)_node->doubleVal;
        return defaultVal;
    }
    long operator|(long defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Int) return _node->longVal;
        return defaultVal;
    }
    float operator|(float defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Float) return (float)_node->doubleVal;
        if (_node->type == JsonNodeType::Int) return (float)_node->longVal;
        return defaultVal;
    }
    double operator|(double defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Float) return _node->doubleVal;
        if (_node->type == JsonNodeType::Int) return (double)_node->longVal;
        return defaultVal;
    }
    unsigned long operator|(unsigned long defaultVal) const {
        if (isNull()) return defaultVal;
        if (_node->type == JsonNodeType::Int) return (unsigned long)_node->longVal;
        return defaultVal;
    }

    // Support for String assignment (Arduino String)
    // Will be enabled if String class is available
    template<typename S>
    auto operator=(const S& s) -> typename std::enable_if<std::is_class<S>::value && !std::is_same<S, std::string>::value, JsonVariant&>::type {
        if (_node) { _node->type = JsonNodeType::String; _node->strVal = s.c_str(); }
        return *this;
    }
};

// ── JsonObject ─────────────────────────────────────────────────────────

class JsonObject {
public:
    std::shared_ptr<JsonNode> _node;

    JsonObject() : _node(std::make_shared<JsonNode>()) { _node->type = JsonNodeType::Object; }
    JsonObject(std::shared_ptr<JsonNode> n) : _node(n) {
        if (_node) _node->type = JsonNodeType::Object;
    }

    bool isNull() const { return !_node; }
    operator bool() const { return _node != nullptr; }

    JsonVariant operator[](const char* key) const {
        if (!_node) return JsonVariant();
        return JsonVariant(_node->objGet(key));
    }
    JsonVariant operator[](const std::string& key) const { return operator[](key.c_str()); }
    // Support Arduino String-like types
    template<typename S>
    auto operator[](const S& key) const -> typename std::enable_if<std::is_class<S>::value && !std::is_same<S, std::string>::value, JsonVariant>::type {
        return operator[](key.c_str());
    }

    bool containsKey(const char* key) const {
        return _node && _node->objHas(key);
    }
    bool containsKey(const std::string& key) const { return containsKey(key.c_str()); }

    size_t size() const { return _node ? _node->objectVals.size() : 0; }

    // Iterator support
    struct Iterator {
        using PairType = std::pair<std::string, std::shared_ptr<JsonNode>>;
        std::vector<PairType>::iterator it;
        Iterator(std::vector<PairType>::iterator i) : it(i) {}
        bool operator!=(const Iterator& o) const { return it != o.it; }
        Iterator& operator++() { ++it; return *this; }
        struct KeyProxy {
            const char* _k;
            const char* c_str() const { return _k; }
            operator const char*() const { return _k; }
        };
        struct Pair {
            KeyProxy _key;
            JsonVariant _value;
            KeyProxy key() const { return _key; }
            JsonVariant value() const { return _value; }
        };
        Pair operator*() { return {KeyProxy{it->first.c_str()}, JsonVariant(it->second)}; }
    };
    Iterator begin() { return Iterator(_node->objectVals.begin()); }
    Iterator end() { return Iterator(_node->objectVals.end()); }
};

// ── JsonArray ──────────────────────────────────────────────────────────

class JsonArray {
public:
    std::shared_ptr<JsonNode> _node;

    JsonArray() : _node(std::make_shared<JsonNode>()) { _node->type = JsonNodeType::Array; }
    JsonArray(std::shared_ptr<JsonNode> n) : _node(n) {
        if (_node) _node->type = JsonNodeType::Array;
    }

    bool isNull() const { return !_node; }
    operator bool() const { return _node != nullptr; }

    size_t size() const { return _node ? _node->arrayVals.size() : 0; }

    JsonVariant operator[](int index) const {
        if (!_node || index < 0 || (size_t)index >= _node->arrayVals.size()) return JsonVariant();
        return JsonVariant(_node->arrayVals[index]);
    }
    JsonVariant operator[](size_t index) const { return operator[]((int)index); }

    JsonVariant add() {
        auto n = std::make_shared<JsonNode>();
        _node->arrayVals.push_back(n);
        return JsonVariant(n);
    }

    template<typename T> T add();

    // add with value
    void add(const char* s) { auto v = add(); v = s; }
    void add(const std::string& s) { auto v = add(); v = s.c_str(); }
    void add(int val) { auto v = add(); v = val; }
    void add(long val) { auto v = add(); v = val; }
    void add(float val) { auto v = add(); v = (double)val; }
    void add(double val) { auto v = add(); v = val; }
    void add(bool val) { auto v = add(); v = val; }
    // add String-like types
    template<typename S>
    auto add(const S& s) -> typename std::enable_if<std::is_class<S>::value && !std::is_same<S, std::string>::value>::type {
        auto v = add(); v = s.c_str();
    }

    JsonObject addObject();
    JsonArray addArray();

    // Iterator
    struct Iterator {
        std::vector<std::shared_ptr<JsonNode>>::iterator it;
        Iterator(std::vector<std::shared_ptr<JsonNode>>::iterator i) : it(i) {}
        bool operator!=(const Iterator& o) const { return it != o.it; }
        Iterator& operator++() { ++it; return *this; }
        JsonVariant operator*() { return JsonVariant(*it); }
    };
    Iterator begin() { return Iterator(_node->arrayVals.begin()); }
    Iterator end() { return Iterator(_node->arrayVals.end()); }
};

// ── JsonVariant conversion operators ───────────────────────────────────

inline JsonVariant::operator JsonObject() const {
    if (_node && (_node->type == JsonNodeType::Object || _node->type == JsonNodeType::Null)) {
        if (_node->type == JsonNodeType::Null) _node->type = JsonNodeType::Object;
        return JsonObject(_node);
    }
    return JsonObject();
}

inline JsonVariant::operator JsonArray() const {
    if (_node && (_node->type == JsonNodeType::Array || _node->type == JsonNodeType::Null)) {
        if (_node->type == JsonNodeType::Null) _node->type = JsonNodeType::Array;
        return JsonArray(_node);
    }
    return JsonArray();
}

// ── JsonVariantConst ───────────────────────────────────────────────────

using JsonVariantConst = JsonVariant;

// ── JsonPair ───────────────────────────────────────────────────────────

using JsonPair = JsonObject::Iterator::Pair;

// ── JsonArray::add<T> specializations ──────────────────────────────────

template<> inline JsonObject JsonArray::add<JsonObject>() {
    auto n = std::make_shared<JsonNode>();
    n->type = JsonNodeType::Object;
    _node->arrayVals.push_back(n);
    return JsonObject(n);
}

template<> inline JsonArray JsonArray::add<JsonArray>() {
    auto n = std::make_shared<JsonNode>();
    n->type = JsonNodeType::Array;
    _node->arrayVals.push_back(n);
    return JsonArray(n);
}

inline JsonObject JsonArray::addObject() { return add<JsonObject>(); }
inline JsonArray JsonArray::addArray() { return add<JsonArray>(); }

// ── Template specializations for as<T> ─────────────────────────────────

template<> inline const char* JsonVariant::as<const char*>() const {
    if (!_node) return nullptr;
    if (_node->type == JsonNodeType::String) return _node->strVal.c_str();
    if (_node->type == JsonNodeType::Null) return nullptr;
    return nullptr;
}

template<> inline std::string JsonVariant::as<std::string>() const {
    const char* s = as<const char*>();
    return s ? s : "";
}

template<> inline bool JsonVariant::as<bool>() const {
    if (!_node) return false;
    if (_node->type == JsonNodeType::Bool) return _node->boolVal;
    if (_node->type == JsonNodeType::Int) return _node->longVal != 0;
    return false;
}

template<> inline int JsonVariant::as<int>() const {
    if (!_node) return 0;
    if (_node->type == JsonNodeType::Int) return (int)_node->longVal;
    if (_node->type == JsonNodeType::Float) return (int)_node->doubleVal;
    if (_node->type == JsonNodeType::Bool) return _node->boolVal ? 1 : 0;
    return 0;
}

template<> inline long JsonVariant::as<long>() const {
    if (!_node) return 0;
    if (_node->type == JsonNodeType::Int) return _node->longVal;
    if (_node->type == JsonNodeType::Float) return (long)_node->doubleVal;
    return 0;
}

template<> inline unsigned long JsonVariant::as<unsigned long>() const {
    return (unsigned long)as<long>();
}

template<> inline float JsonVariant::as<float>() const {
    if (!_node) return 0;
    if (_node->type == JsonNodeType::Float) return (float)_node->doubleVal;
    if (_node->type == JsonNodeType::Int) return (float)_node->longVal;
    return 0;
}

template<> inline double JsonVariant::as<double>() const {
    if (!_node) return 0;
    if (_node->type == JsonNodeType::Float) return _node->doubleVal;
    if (_node->type == JsonNodeType::Int) return (double)_node->longVal;
    return 0;
}

template<> inline JsonObject JsonVariant::as<JsonObject>() const {
    if (_node && (_node->type == JsonNodeType::Object || _node->type == JsonNodeType::Null)) {
        if (_node->type == JsonNodeType::Null) _node->type = JsonNodeType::Object;
        return JsonObject(_node);
    }
    return JsonObject(std::make_shared<JsonNode>());
}

template<> inline JsonArray JsonVariant::as<JsonArray>() const {
    if (_node && (_node->type == JsonNodeType::Array || _node->type == JsonNodeType::Null)) {
        if (_node->type == JsonNodeType::Null) _node->type = JsonNodeType::Array;
        return JsonArray(_node);
    }
    return JsonArray(std::make_shared<JsonNode>());
}

template<> inline JsonVariant JsonVariant::as<JsonVariant>() const {
    return *this;
}

// ── Template specializations for is<T> ─────────────────────────────────

template<> inline bool JsonVariant::is<JsonArray>() const {
    return _node && _node->type == JsonNodeType::Array;
}
template<> inline bool JsonVariant::is<JsonObject>() const {
    return _node && _node->type == JsonNodeType::Object;
}
template<> inline bool JsonVariant::is<const char*>() const {
    return _node && _node->type == JsonNodeType::String;
}
template<> inline bool JsonVariant::is<bool>() const {
    return _node && _node->type == JsonNodeType::Bool;
}
template<> inline bool JsonVariant::is<int>() const {
    return _node && _node->type == JsonNodeType::Int;
}
template<> inline bool JsonVariant::is<float>() const {
    return _node && (_node->type == JsonNodeType::Float || _node->type == JsonNodeType::Int);
}
template<> inline bool JsonVariant::is<JsonVariant>() const {
    return _node && _node->type != JsonNodeType::Null;
}

template<> inline String JsonVariant::as<String>() const {
    const char* s = as<const char*>();
    return s ? String(s) : String("");
}

template<> inline unsigned int JsonVariant::as<unsigned int>() const {
    return (unsigned int)as<int>();
}

template<> inline bool JsonVariant::is<double>() const {
    return is<float>();
}

template<> inline bool JsonVariant::is<long>() const {
    return is<int>();
}

// ── Template specializations for to<T> ─────────────────────────────────

template<> inline JsonObject JsonVariant::to<JsonObject>() {
    if (_node) {
        _node->type = JsonNodeType::Object;
        _node->objectVals.clear();
        _node->arrayVals.clear();
    }
    return JsonObject(_node);
}

template<> inline JsonArray JsonVariant::to<JsonArray>() {
    if (_node) {
        _node->type = JsonNodeType::Array;
        _node->arrayVals.clear();
        _node->objectVals.clear();
    }
    return JsonArray(_node);
}

// ── JsonDocument ───────────────────────────────────────────────────────

class JsonDocument {
public:
    std::shared_ptr<JsonNode> _root;

    JsonDocument() : _root(std::make_shared<JsonNode>()) {}

    JsonVariant operator[](const char* key) {
        if (_root->type == JsonNodeType::Null) _root->type = JsonNodeType::Object;
        if (_root->type != JsonNodeType::Object) return JsonVariant();
        return JsonVariant(_root->objGet(key));
    }
    JsonVariant operator[](const char* key) const {
        if (_root->type != JsonNodeType::Object) return JsonVariant();
        for (auto& p : _root->objectVals) {
            if (p.first == key) return JsonVariant(p.second);
        }
        return JsonVariant();
    }
    JsonVariant operator[](const std::string& key) { return operator[](key.c_str()); }
    JsonVariant operator[](const std::string& key) const { return operator[](key.c_str()); }

    template<typename T> T to() {
        JsonVariant v(_root);
        return v.to<T>();
    }

    template<typename T> T as() {
        return JsonVariant(_root).as<T>();
    }

    template<typename T> T as() const {
        return JsonVariant(_root).as<T>();
    }

    template<typename T> bool is() const {
        return JsonVariant(_root).is<T>();
    }

    bool isNull() const { return !_root || _root->type == JsonNodeType::Null; }
    bool containsKey(const char* key) const { return JsonVariant(_root).containsKey(key); }
    size_t size() const { return _root ? _root->size() : 0; }

    void clear() { _root = std::make_shared<JsonNode>(); }
};

// ── JSON Serialization ─────────────────────────────────────────────────

namespace _ajson_detail {
    inline void escapeString(std::string& out, const std::string& s) {
        out += '"';
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c;
            }
        }
        out += '"';
    }

    inline void serialize(std::string& out, const std::shared_ptr<JsonNode>& node) {
        if (!node || node->type == JsonNodeType::Null) { out += "null"; return; }
        switch (node->type) {
            case JsonNodeType::Bool: out += node->boolVal ? "true" : "false"; break;
            case JsonNodeType::Int: out += std::to_string(node->longVal); break;
            case JsonNodeType::Float: {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", node->doubleVal);
                out += buf;
                break;
            }
            case JsonNodeType::String: escapeString(out, node->strVal); break;
            case JsonNodeType::Object: {
                out += '{';
                bool first = true;
                for (auto& p : node->objectVals) {
                    if (p.second && p.second->type == JsonNodeType::Null) continue;
                    if (!first) out += ',';
                    escapeString(out, p.first);
                    out += ':';
                    serialize(out, p.second);
                    first = false;
                }
                out += '}';
                break;
            }
            case JsonNodeType::Array: {
                out += '[';
                for (size_t i = 0; i < node->arrayVals.size(); i++) {
                    if (i > 0) out += ',';
                    serialize(out, node->arrayVals[i]);
                }
                out += ']';
                break;
            }
            default: out += "null";
        }
    }
}

// serializeJson to std::string
inline size_t serializeJson(const JsonDocument& doc, std::string& output) {
    output.clear();
    _ajson_detail::serialize(output, doc._root);
    return output.size();
}

// serializeJson to String (Arduino-compatible)
template<typename S>
inline auto serializeJson(const JsonDocument& doc, S& output) ->
    typename std::enable_if<std::is_class<S>::value && !std::is_same<S, std::string>::value, size_t>::type {
    std::string tmp;
    _ajson_detail::serialize(tmp, doc._root);
    // Use the write interface
    for (char c : tmp) output.write((uint8_t)c);
    return tmp.size();
}

inline size_t measureJson(const JsonDocument& doc) {
    std::string tmp;
    _ajson_detail::serialize(tmp, doc._root);
    return tmp.size();
}

// ── JSON Deserialization (minimal) ─────────────────────────────────────

struct DeserializationError {
    enum Code { Ok = 0, InvalidInput = 1, NoMemory = 2, TooDeep = 3, EmptyInput = 4 };
    Code code;
    DeserializationError(Code c = Ok) : code(c) {}
    explicit operator bool() const { return code != Ok; }
    bool operator==(Code c) const { return code == c; }
    bool operator!=(Code c) const { return code != c; }
    const char* c_str() const {
        switch (code) {
            case Ok: return "Ok";
            case InvalidInput: return "InvalidInput";
            case NoMemory: return "NoMemory";
            case TooDeep: return "TooDeep";
            case EmptyInput: return "EmptyInput";
            default: return "Unknown";
        }
    }
};

namespace _ajson_detail {
    inline void skipWhitespace(const char*& p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    }

    inline std::shared_ptr<JsonNode> parseValue(const char*& p, int depth = 0);

    inline std::string parseString(const char*& p) {
        std::string result;
        if (*p != '"') return result;
        p++; // skip opening quote
        while (*p && *p != '"') {
            if (*p == '\\') {
                p++;
                switch (*p) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': result += "\\u"; break; // simplified
                    default: result += *p;
                }
            } else {
                result += *p;
            }
            p++;
        }
        if (*p == '"') p++; // skip closing quote
        return result;
    }

    inline std::shared_ptr<JsonNode> parseValue(const char*& p, int depth) {
        if (depth > 50) return nullptr;
        skipWhitespace(p);
        if (!*p) return nullptr;

        auto node = std::make_shared<JsonNode>();

        if (*p == '"') {
            node->type = JsonNodeType::String;
            node->strVal = parseString(p);
        } else if (*p == '{') {
            node->type = JsonNodeType::Object;
            p++; // skip {
            skipWhitespace(p);
            if (*p == '}') { p++; return node; }
            while (*p) {
                skipWhitespace(p);
                if (*p != '"') return nullptr;
                std::string key = parseString(p);
                skipWhitespace(p);
                if (*p != ':') return nullptr;
                p++;
                auto val = parseValue(p, depth + 1);
                if (!val) return nullptr;
                node->objectVals.push_back({key, val});
                skipWhitespace(p);
                if (*p == ',') { p++; continue; }
                if (*p == '}') { p++; break; }
                return nullptr;
            }
        } else if (*p == '[') {
            node->type = JsonNodeType::Array;
            p++;
            skipWhitespace(p);
            if (*p == ']') { p++; return node; }
            while (*p) {
                auto val = parseValue(p, depth + 1);
                if (!val) return nullptr;
                node->arrayVals.push_back(val);
                skipWhitespace(p);
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; break; }
                return nullptr;
            }
        } else if (*p == 't' && strncmp(p, "true", 4) == 0) {
            node->type = JsonNodeType::Bool; node->boolVal = true; p += 4;
        } else if (*p == 'f' && strncmp(p, "false", 5) == 0) {
            node->type = JsonNodeType::Bool; node->boolVal = false; p += 5;
        } else if (*p == 'n' && strncmp(p, "null", 4) == 0) {
            node->type = JsonNodeType::Null; p += 4;
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            const char* start = p;
            bool isFloat = false;
            if (*p == '-') p++;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.') { isFloat = true; p++; while (*p >= '0' && *p <= '9') p++; }
            if (*p == 'e' || *p == 'E') { isFloat = true; p++; if (*p == '+' || *p == '-') p++; while (*p >= '0' && *p <= '9') p++; }
            std::string numStr(start, p);
            if (isFloat) {
                node->type = JsonNodeType::Float;
                node->doubleVal = atof(numStr.c_str());
            } else {
                node->type = JsonNodeType::Int;
                node->longVal = atol(numStr.c_str());
            }
        } else {
            return nullptr;
        }
        return node;
    }
}

// serialized() — wraps a pre-serialized JSON value (raw passthrough)
// In real ArduinoJson this returns a RawJson type; here we just return a variant with the string.
inline JsonVariant serialized(const String& s) {
    static JsonDocument _sdoc;
    _sdoc.clear();
    _sdoc["v"] = std::string(s.c_str());
    return _sdoc["v"];
}

inline DeserializationError deserializeJson(JsonDocument& doc, const char* json) {
    if (!json || !*json) return DeserializationError::EmptyInput;
    const char* p = json;
    _ajson_detail::skipWhitespace(p);
    auto node = _ajson_detail::parseValue(p);
    if (!node) return DeserializationError::InvalidInput;
    doc._root = node;
    return DeserializationError::Ok;
}

inline DeserializationError deserializeJson(JsonDocument& doc, const std::string& json) {
    return deserializeJson(doc, json.c_str());
}

// Support Arduino String
template<typename S>
inline auto deserializeJson(JsonDocument& doc, const S& input) ->
    typename std::enable_if<std::is_class<S>::value && !std::is_same<S, std::string>::value, DeserializationError>::type {
    return deserializeJson(doc, input.c_str());
}
