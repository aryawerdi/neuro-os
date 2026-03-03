#ifndef NEURO_OS_UTILS_CONFIG_ENHANCED_HPP
#define NEURO_OS_UTILS_CONFIG_ENHANCED_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstdlib>

namespace neuro_os::utils {

// Configuration value types
class ConfigValue {
public:
    enum class Type {
        STRING,
        INT,
        DOUBLE,
        BOOL,
        ARRAY,
        OBJECT,
        NULL_VALUE
    };
    
    ConfigValue() : type_(Type::NULL_VALUE) {}
    
    explicit ConfigValue(const std::string& value) : type_(Type::STRING), string_value_(value) {}
    explicit ConfigValue(int value) : type_(Type::INT), int_value_(value) {}
    explicit ConfigValue(double value) : type_(Type::DOUBLE), double_value_(value) {}
    explicit ConfigValue(bool value) : type_(Type::BOOL), bool_value_(value) {}
    
    Type type() const { return type_; }
    
    std::string as_string() const {
        switch (type_) {
            case Type::STRING: return string_value_;
            case Type::INT: return std::to_string(int_value_);
            case Type::DOUBLE: {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(6) << double_value_;
                return ss.str();
            }
            case Type::BOOL: return bool_value_ ? "true" : "false";
            case Type::ARRAY: return "[array]";
            case Type::OBJECT: return "{object}";
            case Type::NULL_VALUE: return "null";
        }
        return "";
    }
    
    int as_int() const {
        switch (type_) {
            case Type::INT: return int_value_;
            case Type::DOUBLE: return static_cast<int>(double_value_);
            case Type::BOOL: return bool_value_ ? 1 : 0;
            case Type::STRING: return std::stoi(string_value_);
            default: return 0;
        }
    }
    
    double as_double() const {
        switch (type_) {
            case Type::DOUBLE: return double_value_;
            case Type::INT: return static_cast<double>(int_value_);
            case Type::BOOL: return bool_value_ ? 1.0 : 0.0;
            case Type::STRING: return std::stod(string_value_);
            default: return 0.0;
        }
    }
    
    bool as_bool() const {
        switch (type_) {
            case Type::BOOL: return bool_value_;
            case Type::INT: return int_value_ != 0;
            case Type::DOUBLE: return double_value_ != 0.0;
            case Type::STRING: return string_value_ == "true" || string_value_ == "1";
            default: return false;
        }
    }
    
    bool is_null() const { return type_ == Type::NULL_VALUE; }
    
private:
    Type type_;
    std::string string_value_;
    int int_value_{0};
    double double_value_{0.0};
    bool bool_value_{false};
};

// Configuration schema for validation
struct ConfigSchema {
    struct Field {
        std::string name;
        ConfigValue::Type type;
        ConfigValue default_value;
        bool required;
        std::string description;
        
        Field(const std::string& n, ConfigValue::Type t, const ConfigValue& def, bool req = false, const std::string& desc = "")
            : name(n), type(t), default_value(def), required(req), description(desc) {}
    };
    
    std::vector<Field> fields;
    
    void add_field(const Field& field) {
        fields.push_back(field);
    }
    
    bool validate(const std::map<std::string, ConfigValue>& config, std::string& error) const {
        for (const auto& field : fields) {
            auto it = config.find(field.name);
            if (it == config.end()) {
                if (field.required) {
                    error = "Missing required field: " + field.name;
                    return false;
                }
                continue;
            }
            
            if (it->second.type() != field.type) {
                error = "Field " + field.name + " has wrong type";
                return false;
            }
        }
        return true;
    }
};

// Enhanced Configuration with hot reload, validation, and environment variable interpolation
class EnhancedConfig {
public:
    using ConfigMap = std::map<std::string, ConfigValue>;
    using ChangeCallback = std::function<void(const std::string& key, const ConfigValue& old_value, const ConfigValue& new_value)>;
    
    EnhancedConfig() : stop_watcher_(false) {}
    
    ~EnhancedConfig() {
        stop_watcher_ = true;
        if (watcher_thread_.joinable()) {
            watcher_thread_.join();
        }
    }
    
    // Load configuration from map
    void load(const ConfigMap& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        apply_environment_variables();
        notify_change_listeners();
    }
    
    // Set a configuration value
    void set(const std::string& key, const ConfigValue& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        ConfigValue old_value = get_unsafe(key);
        config_[key] = value;
        notify_change(key, old_value, value);
    }
    
    // Get a configuration value with default
    ConfigValue get(const std::string& key, const ConfigValue& default_value = ConfigValue()) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = config_.find(key);
        if (it != config_.end()) {
            return it->second;
        }
        return default_value;
    }
    
    // Get string value with environment variable interpolation
    std::string get_string(const std::string& key, const std::string& default_value = "") const {
        std::string value = get(key, ConfigValue(default_value)).as_string();
        return interpolate_environment_variables(value);
    }
    
    // Get int value
    int get_int(const std::string& key, int default_value = 0) const {
        return get(key, ConfigValue(default_value)).as_int();
    }
    
    // Get double value
    double get_double(const std::string& key, double default_value = 0.0) const {
        return get(key, ConfigValue(default_value)).as_double();
    }
    
    // Get bool value
    bool get_bool(const std::string& key, bool default_value = false) const {
        return get(key, ConfigValue(default_value)).as_bool();
    }
    
    // Check if key exists
    bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_.find(key) != config_.end();
    }
    
    // Get all configuration keys
    std::vector<std::string> keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> result;
        for (const auto& pair : config_) {
            result.push_back(pair.first);
        }
        return result;
    }
    
    // Register change callback
    void register_change_callback(const std::string& key, ChangeCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        change_callbacks_[key].push_back(callback);
    }
    
    // Register schema for validation
    void set_schema(const ConfigSchema& schema) {
        std::lock_guard<std::mutex> lock(mutex_);
        schema_ = schema;
    }
    
    // Validate configuration against schema
    bool validate(std::string& error) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (schema_.fields.empty()) {
            return true; // No schema means everything is valid
        }
        return schema_.validate(config_, error);
    }
    
    // Start hot reload watcher (simulated - in real implementation would watch file)
    void start_hot_reload_watcher(std::chrono::milliseconds interval = std::chrono::seconds(5)) {
        watcher_thread_ = std::thread([this, interval]() {
            while (!stop_watcher_) {
                std::this_thread::sleep_for(interval);
                // In a real implementation, this would check file modification time
                // and reload if changed
            }
        });
    }
    
    // Stop hot reload watcher
    void stop_hot_reload_watcher() {
        stop_watcher_ = true;
        if (watcher_thread_.joinable()) {
            watcher_thread_.join();
        }
    }
    
    // Export configuration as string
    std::string export_as_string() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::stringstream ss;
        ss << "Configuration (" << config_.size() << " items):\n";
        for (const auto& pair : config_) {
            ss << "  " << pair.first << " = " << pair.second.as_string() << "\n";
        }
        return ss.str();
    }
    
    // Secret management: mask sensitive values
    void mark_as_secret(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        secret_keys_.insert(key);
    }
    
    std::string get_masked(const std::string& key, const std::string& default_value = "") const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (secret_keys_.find(key) != secret_keys_.end()) {
            return "***MASKED***";
        }
        return get_string(key, default_value);
    }
    
private:
    ConfigValue get_unsafe(const std::string& key) const {
        auto it = config_.find(key);
        if (it != config_.end()) {
            return it->second;
        }
        return ConfigValue();
    }
    
    void notify_change(const std::string& key, const ConfigValue& old_value, const ConfigValue& new_value) {
        auto it = change_callbacks_.find(key);
        if (it != change_callbacks_.end()) {
            for (const auto& callback : it->second) {
                try {
                    callback(key, old_value, new_value);
                } catch (...) {
                    // Ignore callback errors
                }
            }
        }
    }
    
    void notify_change_listeners() {
        // Notify all registered callbacks for all keys
        for (const auto& pair : change_callbacks_) {
            const std::string& key = pair.first;
            ConfigValue old_value = ConfigValue(); // Unknown old value
            ConfigValue new_value = get_unsafe(key);
            notify_change(key, old_value, new_value);
        }
    }
    
    void apply_environment_variables() {
        for (auto& pair : config_) {
            if (pair.second.type() == ConfigValue::Type::STRING) {
                std::string value = pair.second.as_string();
                std::string interpolated = interpolate_environment_variables(value);
                if (interpolated != value) {
                    pair.second = ConfigValue(interpolated);
                }
            }
        }
    }
    
    std::string interpolate_environment_variables(const std::string& input) const {
        std::string result = input;
        size_t start_pos = 0;
        
        while ((start_pos = result.find("${", start_pos)) != std::string::npos) {
            size_t end_pos = result.find("}", start_pos);
            if (end_pos == std::string::npos) {
                break;
            }
            
            std::string var_name = result.substr(start_pos + 2, end_pos - start_pos - 2);
            const char* env_value = std::getenv(var_name.c_str());
            
            if (env_value) {
                result.replace(start_pos, end_pos - start_pos + 1, env_value);
                start_pos += strlen(env_value);
            } else {
                start_pos = end_pos + 1;
            }
        }
        
        return result;
    }
    
    mutable std::mutex mutex_;
    ConfigMap config_;
    ConfigSchema schema_;
    std::map<std::string, std::vector<ChangeCallback> > change_callbacks_;
    std::set<std::string> secret_keys_;
    
    std::thread watcher_thread_;
    std::atomic<bool> stop_watcher_;
};

// Global configuration instance
inline EnhancedConfig& get_enhanced_config() {
    static EnhancedConfig instance;
    return instance;
}

// Convenience functions
inline void enhanced_config_set(const std::string& key, const ConfigValue& value) {
    get_enhanced_config().set(key, value);
}

inline ConfigValue enhanced_config_get(const std::string& key, const ConfigValue& default_value = ConfigValue()) {
    return get_enhanced_config().get(key, default_value);
}

inline std::string enhanced_config_get_string(const std::string& key, const std::string& default_value = "") {
    return get_enhanced_config().get_string(key, default_value);
}

inline int enhanced_config_get_int(const std::string& key, int default_value = 0) {
    return get_enhanced_config().get_int(key, default_value);
}

inline double enhanced_config_get_double(const std::string& key, double default_value = 0.0) {
    return get_enhanced_config().get_double(key, default_value);
}

inline bool enhanced_config_get_bool(const std::string& key, bool default_value = false) {
    return get_enhanced_config().get_bool(key, default_value);
}

} // namespace neuro_os::utils

#endif // NEURO_OS_UTILS_CONFIG_ENHANCED_HPP