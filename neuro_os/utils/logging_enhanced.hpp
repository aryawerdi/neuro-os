#ifndef NEURO_OS_UTILS_LOGGING_ENHANCED_HPP
#define NEURO_OS_UTILS_LOGGING_ENHANCED_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace neuro_os::utils {

enum class LogLevel : int {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
};

enum class LogFormat {
    PLAIN,
    JSON,
    CSV
};

enum class RotationPolicy {
    NONE,
    SIZE,
    TIME,
    SIZE_AND_TIME
};

struct LogConfig {
    LogLevel level{LogLevel::INFO};
    LogFormat format{LogFormat::PLAIN};
    bool output_stdout{true};
    std::string output_file;
    size_t max_file_size{10 * 1024 * 1024}; // 10MB
    size_t max_backup_files{5};
    RotationPolicy rotation_policy{RotationPolicy::SIZE};
    std::chrono::hours rotation_interval{24}; // 24 hours
    bool async_logging{true};
    size_t queue_size{1000};
    std::map<std::string, std::string> context_fields;
};

struct LogRecord {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string file;
    int line;
    std::string message;
    std::thread::id thread_id;
    std::map<std::string, std::string> context;
    
    LogRecord(LogLevel lvl, std::string_view f, int ln, std::string_view msg)
        : timestamp(std::chrono::system_clock::now())
        , level(lvl)
        , file(f)
        , line(ln)
        , message(msg)
        , thread_id(std::this_thread::get_id()) {}
};

class AsyncLogQueue {
public:
    explicit AsyncLogQueue(size_t max_size = 1000) 
        : max_size_(max_size), stop_(false) {
        worker_ = std::thread([this] { this->process_queue(); });
    }
    
    ~AsyncLogQueue() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    
    bool enqueue(std::function<void()> log_task) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= max_size_) {
            return false;
        }
        queue_.push(std::move(log_task));
        lock.unlock();
        cv_.notify_one();
        return true;
    }
    
    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_flush_.wait(lock, [this] { return queue_.empty(); });
    }
    
private:
    void process_queue() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                
                if (stop_ && queue_.empty()) {
                    break;
                }
                
                if (!queue_.empty()) {
                    task = std::move(queue_.front());
                    queue_.pop();
                }
            }
            
            if (task) {
                task();
            }
            
            cv_flush_.notify_all();
        }
    }
    
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cv_flush_;
    std::thread worker_;
    size_t max_size_;
    std::atomic<bool> stop_;
};

class LogRotator {
public:
    LogRotator(const std::string& base_path, size_t max_size, size_t max_backups, 
               RotationPolicy policy, std::chrono::hours interval)
        : base_path_(base_path)
        , max_size_(max_size)
        , max_backups_(max_backups)
        , policy_(policy)
        , interval_(interval)
        , last_rotation_(std::chrono::system_clock::now()) {}
    
    bool should_rotate(std::ofstream& stream) const {
        if (policy_ == RotationPolicy::NONE) {
            return false;
        }
        
        bool size_check = false;
        bool time_check = false;
        
        if (policy_ == RotationPolicy::SIZE || policy_ == RotationPolicy::SIZE_AND_TIME) {
            auto pos = stream.tellp();
            size_check = (pos >= 0 && static_cast<size_t>(pos) >= max_size_);
        }
        
        if (policy_ == RotationPolicy::TIME || policy_ == RotationPolicy::SIZE_AND_TIME) {
            auto now = std::chrono::system_clock::now();
            time_check = (now - last_rotation_) >= interval_;
        }
        
        return size_check || time_check;
    }
    
    void rotate(std::ofstream& stream) {
        stream.close();
        
        for (int i = static_cast<int>(max_backups_) - 1; i >= 0; --i) {
            std::filesystem::path old_path = i == 0 ? base_path_ : base_path_ + "." + std::to_string(i);
            std::filesystem::path new_path = base_path_ + "." + std::to_string(i + 1);
            
            if (std::filesystem::exists(old_path)) {
                if (i == static_cast<int>(max_backups_) - 1) {
                    std::filesystem::remove(old_path);
                } else {
                    std::filesystem::rename(old_path, new_path);
                }
            }
        }
        
        stream.open(base_path_, std::ios::app);
        last_rotation_ = std::chrono::system_clock::now();
    }
    
private:
    std::string base_path_;
    size_t max_size_;
    size_t max_backups_;
    RotationPolicy policy_;
    std::chrono::hours interval_;
    std::chrono::system_clock::time_point last_rotation_;
};

class EnhancedLogger {
public:
    static EnhancedLogger& get_instance() {
        static EnhancedLogger instance;
        return instance;
    }

    static EnhancedLogger& instance() {
        return get_instance();
    }

    void configure(const LogConfig& config) {
        std::unique_lock lock(mutex_);
        config_ = config;
        
        if (config_.async_logging && !async_queue_) {
            async_queue_ = std::make_unique<AsyncLogQueue>(config_.queue_size);
        } else if (!config_.async_logging && async_queue_) {
            async_queue_.reset();
        }
        
        if (!config_.output_file.empty()) {
            if (config_.rotation_policy != RotationPolicy::NONE) {
                rotator_ = std::make_unique<LogRotator>(
                    config_.output_file,
                    config_.max_file_size,
                    config_.max_backup_files,
                    config_.rotation_policy,
                    config_.rotation_interval
                );
            }
            file_stream_.open(config_.output_file, std::ios::app);
        }
    }

    void set_level(LogLevel level) {
        config_.level = level;
    }

    void set_format(LogFormat format) {
        config_.format = format;
    }

    void set_context_field(const std::string& key, const std::string& value) {
        std::unique_lock lock(mutex_);
        config_.context_fields[key] = value;
    }

    void remove_context_field(const std::string& key) {
        std::unique_lock lock(mutex_);
        config_.context_fields.erase(key);
    }

    void log(LogLevel level, std::string_view file, int line, std::string_view message) {
        if (static_cast<int>(level) < static_cast<int>(config_.level)) {
            return;
        }

        auto record = std::make_shared<LogRecord>(level, file, line, message);
        record->context = config_.context_fields;

        auto log_task = [this, record]() {
            write_record(*record);
        };

        if (config_.async_logging && async_queue_) {
            if (!async_queue_->enqueue(std::move(log_task))) {
                // Queue full, fall back to synchronous logging
                write_record(*record);
            }
        } else {
            log_task();
        }
    }

    template<typename... Args>
    void log_format(LogLevel level, std::string_view file, int line, const std::string& fmt, Args&&... args) {
        if (static_cast<int>(level) < static_cast<int>(config_.level)) {
            return;
        }

        std::string formatted;
        format_string(formatted, fmt, std::forward<Args>(args)...);
        log(level, file, line, formatted);
    }

    void flush() {
        if (async_queue_) {
            async_queue_->flush();
        }
        if (file_stream_.is_open()) {
            file_stream_.flush();
        }
    }

    LogLevel get_level() const { return config_.level; }
    LogFormat get_format() const { return config_.format; }
    const LogConfig& get_config() const { return config_; }

private:
    EnhancedLogger() = default;
    ~EnhancedLogger() {
        flush();
    }

    EnhancedLogger(const EnhancedLogger&) = delete;
    EnhancedLogger& operator=(const EnhancedLogger&) = delete;

    void write_record(const LogRecord& record) {
        std::string log_line;
        switch (config_.format) {
            case LogFormat::JSON:
                log_line = format_json(record);
                break;
            case LogFormat::CSV:
                log_line = format_csv(record);
                break;
            case LogFormat::PLAIN:
            default:
                log_line = format_plain(record);
                break;
        }

        std::unique_lock lock(mutex_);
        
        if (config_.output_stdout) {
            std::cout << log_line << std::endl;
        }

        if (file_stream_.is_open()) {
            if (rotator_ && rotator_->should_rotate(file_stream_)) {
                rotator_->rotate(file_stream_);
            }
            file_stream_ << log_line << std::endl;
            file_stream_.flush();
        }
    }

    std::string format_plain(const LogRecord& record) const {
        std::stringstream ss;
        ss << "[" << format_timestamp(record.timestamp) << "] "
           << "[" << level_to_string(record.level) << "] "
           << "[" << record.thread_id << "] "
           << record.file << ":" << record.line << " - " << record.message;
        
        if (!record.context.empty()) {
            ss << " [";
            bool first = true;
            for (const auto& [key, value] : record.context) {
                if (!first) ss << ", ";
                ss << key << "=" << value;
                first = false;
            }
            ss << "]";
        }
        
        return ss.str();
    }

    std::string format_json(const LogRecord& record) const {
        std::stringstream ss;
        ss << "{"
           << "\"timestamp\":\"" << format_timestamp_iso(record.timestamp) << "\","
           << "\"level\":\"" << level_to_string(record.level) << "\","
           << "\"thread_id\":\"" << record.thread_id << "\","
           << "\"file\":\"" << record.file << "\","
           << "\"line\":" << record.line << ","
           << "\"message\":\"" << escape_json(record.message) << "\"";
        
        if (!record.context.empty()) {
            ss << ",\"context\":{";
            bool first = true;
            for (const auto& [key, value] : record.context) {
                if (!first) ss << ",";
                ss << "\"" << key << "\":\"" << escape_json(value) << "\"";
                first = false;
            }
            ss << "}";
        }
        
        ss << "}";
        return ss.str();
    }

    std::string format_csv(const LogRecord& record) const {
        std::stringstream ss;
        ss << format_timestamp_iso(record.timestamp) << ","
           << level_to_string(record.level) << ","
           << record.thread_id << ","
           << "\"" << escape_csv(record.file) << "\","
           << record.line << ","
           << "\"" << escape_csv(record.message) << "\"";
        
        for (const auto& [key, value] : record.context) {
            ss << ",\"" << escape_csv(value) << "\"";
        }
        
        return ss.str();
    }

    template<typename... Args>
    void format_string(std::string& out, const std::string& fmt, Args&&... args) {
        size_t size = std::snprintf(nullptr, 0, fmt.c_str(), args...) + 1;
        std::vector<char> buf(size);
        std::snprintf(buf.data(), size, fmt.c_str(), args...);
        out = std::string(buf.data(), buf.data() + size - 1);
    }

    std::string format_timestamp(std::chrono::system_clock::time_point tp) const {
        auto time_t_now = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()
        ) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string format_timestamp_iso(std::chrono::system_clock::time_point tp) const {
        auto time_t_now = std::chrono::system_clock::to_time_t(tp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()
        ) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        return ss.str();
    }

    std::string level_to_string(LogLevel level) const {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
        }
        return "UNKNOWN";
    }

    std::string escape_json(const std::string& str) const {
        std::string result;
        result.reserve(str.length());
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                        char buf[7];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        result += buf;
                    } else {
                        result += c;
                    }
                    break;
            }
        }
        return result;
    }

    std::string escape_csv(const std::string& str) const {
        std::string result;
        bool needs_quotes = false;
        
        for (char c : str) {
            if (c == '"' || c == ',' || c == '\n' || c == '\r') {
                needs_quotes = true;
                if (c == '"') {
                    result += "\"\"";
                } else {
                    result += c;
                }
            } else {
                result += c;
            }
        }
        
        if (needs_quotes) {
            return "\"" + result + "\"";
        }
        return result;
    }

    mutable std::mutex mutex_;
    LogConfig config_;
    std::unique_ptr<AsyncLogQueue> async_queue_;
    std::unique_ptr<LogRotator> rotator_;
    std::ofstream file_stream_;
};

// Convenience macros
#define NEURO_OS_LOG_TRACE(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::TRACE, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_DEBUG(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::DEBUG, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_INFO(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::INFO, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_WARN(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::WARN, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_ERROR(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::ERROR, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_FATAL(msg) \
    neuro_os::utils::EnhancedLogger::instance().log(neuro_os::utils::LogLevel::FATAL, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_TRACE_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::TRACE, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_DEBUG_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::DEBUG, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_INFO_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::INFO, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_WARN_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::WARN, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_ERROR_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::ERROR, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_FATAL_F(fmt, ...) \
    neuro_os::utils::EnhancedLogger::instance().log_format(neuro_os::utils::LogLevel::FATAL, __FILE__, __LINE__, fmt, __VA_ARGS__)

}

#endif // NEURO_OS_UTILS_LOGGING_ENHANCED_HPP