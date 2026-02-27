#ifndef NEURO_OS_UTILS_CONFIG_HPP
#define NEURO_OS_UTILS_CONFIG_HPP

#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neuro_os::utils {

using ConfigValue = std::variant<
    std::string,
    int64_t,
    double,
    bool,
    std::vector<std::string>
>;

class Config {
public:
    Config() = default;

    explicit Config(const std::string& yaml_or_json_content) {
        parse(yaml_or_json_content);
    }

    void set(std::string_view key, const ConfigValue& value);
    void set(std::string_view key, ConfigValue&& value);

    template<typename T>
    std::optional<T> get(std::string_view key) const;

    template<typename T>
    T get_or(std::string_view key, T default_value) const;

    template<typename T>
    bool try_get(T& out, std::string_view key) const;

    bool has(std::string_view key) const;

    void remove(std::string_view key);

    void load_env_vars(const std::string& prefix = "NEURO_OS_");

    std::string dump_json() const;
    std::string dump_yaml() const;

    void merge(const Config& other);

private:
    void parse(const std::string& content);
    void parse_json(const std::string& content, size_t& pos);
    void parse_json_value(const std::string& content, size_t& pos, ConfigValue& out);
    std::string parse_json_string(const std::string& content, size_t& pos);
    void skip_whitespace(const std::string& content, size_t& pos);

    std::map<std::string, ConfigValue> values_;
};

inline void Config::set(std::string_view key, const ConfigValue& value) {
    values_[std::string(key)] = value;
}

inline void Config::set(std::string_view key, ConfigValue&& value) {
    values_[std::string(key)] = std::move(value);
}

template<typename T>
inline std::optional<T> Config::get(std::string_view key) const {
    auto it = values_.find(std::string(key));
    if (it == values_.end()) {
        return std::nullopt;
    }
    
    if (auto* ptr = std::get_if<T>(&it->second)) {
        return *ptr;
    }
    return std::nullopt;
}

template<typename T>
inline T Config::get_or(std::string_view key, T default_value) const {
    auto result = get<T>(key);
    return result.value_or(default_value);
}

template<typename T>
inline bool Config::try_get(T& out, std::string_view key) const {
    auto result = get<T>(key);
    if (result) {
        out = *result;
        return true;
    }
    return false;
}

inline bool Config::has(std::string_view key) const {
    return values_.find(std::string(key)) != values_.end();
}

inline void Config::remove(std::string_view key) {
    values_.erase(std::string(key));
}

inline void Config::load_env_vars(const std::string& prefix) {
    for (auto& [key, value] : values_) {
        std::string env_var = prefix;
        for (char c : key) {
            if (c == '.') {
                env_var += '_';
            } else {
                env_var += toupper(c);
            }
        }
        
        const char* env_value = std::getenv(env_var.c_str());
        if (env_value != nullptr) {
            std::string str_val(env_value);
            
            if (auto* str_ptr = std::get_if<std::string>(&value)) {
                values_[key] = str_val;
            } else if (auto* int_ptr = std::get_if<int64_t>(&value)) {
                try {
                    values_[key] = std::stoll(str_val);
                } catch (...) {}
            } else if (auto* dbl_ptr = std::get_if<double>(&value)) {
                try {
                    values_[key] = std::stod(str_val);
                } catch (...) {}
            } else if (auto* bool_ptr = std::get_if<bool>(&value)) {
                values_[key] = (str_val == "1" || str_val == "true" || str_val == "yes");
            }
        }
    }
}

inline void Config::skip_whitespace(const std::string& content, size_t& pos) {
    while (pos < content.size() && std::isspace(content[pos])) {
        ++pos;
    }
}

inline std::string Config::parse_json_string(const std::string& content, size_t& pos) {
    ++pos;
    std::string result;
    while (pos < content.size() && content[pos] != '"') {
        if (content[pos] == '\\' && pos + 1 < content.size()) {
            ++pos;
            switch (content[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += content[pos]; break;
            }
        } else {
            result += content[pos];
        }
        ++pos;
    }
    ++pos;
    return result;
}

inline void Config::parse_json_value(const std::string& content, size_t& pos, ConfigValue& out) {
    skip_whitespace(content, pos);
    
    if (pos >= content.size()) return;
    
    char c = content[pos];
    
    if (c == '"') {
        out = parse_json_string(content, pos);
    } else if (c == 't' || c == 'f') {
        std::string bool_str;
        while (pos < content.size() && (isalpha(content[pos]) || content[pos] == '_')) {
            bool_str += content[pos++];
        }
        out = (bool_str == "true");
    } else if (c == '[') {
        ++pos;
        skip_whitespace(content, pos);
        std::vector<std::string> arr;
        
        if (content[pos] != ']') {
            while (true) {
                ConfigValue elem;
                parse_json_value(content, pos, elem);
                
                if (auto* str = std::get_if<std::string>(&elem)) {
                    arr.push_back(*str);
                } else if (auto* num = std::get_if<int64_t>(&elem)) {
                    arr.push_back(std::to_string(*num));
                } else if (auto* dbl = std::get_if<double>(&elem)) {
                    arr.push_back(std::to_string(*dbl));
                }
                
                skip_whitespace(content, pos);
                if (content[pos] == ']') break;
                ++pos;
            }
        }
        ++pos;
        out = arr;
    } else if (c == '{') {
        ++pos;
        Config nested;
        while (pos < content.size() && content[pos] != '}') {
            skip_whitespace(content, pos);
            if (content[pos] == '"') {
                std::string key = parse_json_string(content, pos);
                skip_whitespace(content, pos);
                if (content[pos] == ':') ++pos;
                skip_whitespace(content, pos);
                ConfigValue value;
                parse_json_value(content, pos, value);
                nested.set(key, value);
                skip_whitespace(content, pos);
                if (content[pos] == ',') ++pos;
            }
        }
        ++pos;
    } else {
        std::string num_str;
        bool has_dot = false;
        while (pos < content.size() && (isdigit(content[pos]) || content[pos] == '.' || content[pos] == '-' || content[pos] == '+' || content[pos] == 'e' || content[pos] == 'E')) {
            if (content[pos] == '.') has_dot = true;
            num_str += content[pos++];
        }
        
        if (has_dot) {
            try {
                out = std::stod(num_str);
            } catch (...) {
                out = 0.0;
            }
        } else {
            try {
                out = std::stoll(num_str);
            } catch (...) {
                out = int64_t(0);
            }
        }
    }
}

inline void Config::parse_json(const std::string& content, size_t& pos) {
    skip_whitespace(content, pos);
    
    if (content[pos] == '{') {
        ++pos;
        while (pos < content.size() && content[pos] != '}') {
            skip_whitespace(content, pos);
            
            if (content[pos] == '"') {
                std::string key = parse_json_string(content, pos);
                skip_whitespace(content, pos);
                
                if (content[pos] == ':') ++pos;
                skip_whitespace(content, pos);
                
                ConfigValue value;
                parse_json_value(content, pos, value);
                set(key, value);
                
                skip_whitespace(content, pos);
                if (content[pos] == ',') ++pos;
            }
        }
        ++pos;
    }
}

inline void Config::parse(const std::string& content) {
    size_t pos = 0;
    
    skip_whitespace(content, pos);
    if (content[pos] == '{') {
        parse_json(content, pos);
    }
}

inline std::string Config::dump_json() const {
    std::stringstream ss;
    ss << "{\n";
    
    bool first = true;
    for (const auto& [key, value] : values_) {
        if (!first) ss << ",\n";
        
        ss << "  \"" << key << "\": ";
        
        if (auto* v = std::get_if<std::string>(&value)) {
            ss << "\"" << *v << "\"";
        } else if (auto* v = std::get_if<bool>(&value)) {
            ss << (*v ? "true" : "false");
        } else if (auto* v = std::get_if<int64_t>(&value)) {
            ss << *v;
        } else if (auto* v = std::get_if<double>(&value)) {
            ss << std::fixed << std::setprecision(6) << *v;
        } else if (auto* v = std::get_if<std::vector<std::string>>(&value)) {
            ss << "[";
            bool f = true;
            for (const auto& s : *v) {
                if (!f) ss << ", ";
                ss << "\"" << s << "\"";
                f = false;
            }
            ss << "]";
        }
        
        first = false;
    }
    
    ss << "\n}";
    return ss.str();
}

inline std::string Config::dump_yaml() const {
    std::stringstream ss;
    
    for (const auto& [key, value] : values_) {
        ss << key << ": ";
        
        if (auto* v = std::get_if<std::string>(&value)) {
            if (v->find(':') != std::string::npos || v->find('#') != std::string::npos) {
                ss << "\"" << *v << "\"";
            } else {
                ss << *v;
            }
        } else if (auto* v = std::get_if<bool>(&value)) {
            ss << (*v ? "true" : "false");
        } else if (auto* v = std::get_if<int64_t>(&value)) {
            ss << *v;
        } else if (auto* v = std::get_if<double>(&value)) {
            ss << std::fixed << std::setprecision(6) << *v;
        } else if (auto* v = std::get_if<std::vector<std::string>>(&value)) {
            ss << "\n";
            for (const auto& s : *v) {
                ss << "  - " << s << "\n";
            }
        }
        
        ss << "\n";
    }
    
    return ss.str();
}

inline void Config::merge(const Config& other) {
    for (const auto& [key, value] : other.values_) {
        values_[key] = value;
    }
}

class ConfigBuilder {
public:
    ConfigBuilder& add(std::string_view key, const ConfigValue& value) {
        config_.set(key, value);
        return *this;
    }

    ConfigBuilder& add_default(std::string_view key, const ConfigValue& value) {
        if (!config_.has(key)) {
            config_.set(key, value);
        }
        return *this;
    }

    Config build() {
        return std::move(config_);
    }

    Config& get_config() { return config_; }

private:
    Config config_;
};

}

#endif
