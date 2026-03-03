#ifndef NEURO_OS_UTILS_LOGGING_HPP
#define NEURO_OS_UTILS_LOGGING_HPP

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <iomanip>

namespace neuro_os::utils {

enum class LogLevel : int {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& get_instance() {
        static Logger instance;
        return instance;
    }

    static Logger& instance() {
        return get_instance();
    }

    void set_level(LogLevel level) {
        level_.store(static_cast<int>(level));
    }

    void set_output_file(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_output_.reset(new std::ofstream(filename, std::ios::app));
    }

    void set_output_file(std::ostream* os) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_output_.reset(os);
    }

    void set_output_stdout(bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);
        output_stdout_ = enable;
    }

    void log(LogLevel level, std::string_view file, int line, std::string_view message) {
        if (static_cast<int>(level) < level_.load()) {
            return;
        }

        std::stringstream ss;
        ss << "[" << format_timestamp() << "] "
           << "[" << level_to_string(level) << "] "
           << file << ":" << line << " - " << message;

        std::string log_line = ss.str();

        std::lock_guard<std::mutex> lock(mutex_);

        if (output_stdout_) {
            std::cout << log_line << std::endl;
        }

        if (file_output_) {
            *file_output_ << log_line << std::endl;
            file_output_->flush();
        }
    }

    template<typename... Args>
    void log_format(LogLevel level, std::string_view file, int line, const std::string& fmt, Args&&... args) {
        if (static_cast<int>(level) < level_.load()) {
            return;
        }

        std::stringstream ss;
        ((ss << args), ...);
        
        log(level, file, line, ss.str());
    }

    LogLevel get_level() const {
        return static_cast<LogLevel>(level_.load());
    }

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string format_timestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string level_to_string(LogLevel level) const {
        if (level == LogLevel::DEBUG) return "DEBUG";
        if (level == LogLevel::INFO) return "INFO ";
        if (level == LogLevel::WARN) return "WARN ";
        if (level == LogLevel::ERROR) return "ERROR";
        return "UNKNOWN";
    }

    std::mutex mutex_;
    std::atomic<int> level_;
    std::unique_ptr<std::ostream> file_output_;
    bool output_stdout_;
};

#define NEURO_OS_LOG_DEBUG(msg) \
    neuro_os::utils::Logger::instance().log(neuro_os::utils::LogLevel::DEBUG, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_INFO(msg) \
    neuro_os::utils::Logger::instance().log(neuro_os::utils::LogLevel::INFO, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_WARN(msg) \
    neuro_os::utils::Logger::instance().log(neuro_os::utils::LogLevel::WARN, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_ERROR(msg) \
    neuro_os::utils::Logger::instance().log(neuro_os::utils::LogLevel::ERROR, __FILE__, __LINE__, msg)

#define NEURO_OS_LOG_DEBUG_F(fmt, ...) \
    neuro_os::utils::Logger::instance().log_format(neuro_os::utils::LogLevel::DEBUG, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_INFO_F(fmt, ...) \
    neuro_os::utils::Logger::instance().log_format(neuro_os::utils::LogLevel::INFO, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_WARN_F(fmt, ...) \
    neuro_os::utils::Logger::instance().log_format(neuro_os::utils::LogLevel::WARN, __FILE__, __LINE__, fmt, __VA_ARGS__)

#define NEURO_OS_LOG_ERROR_F(fmt, ...) \
    neuro_os::utils::Logger::instance().log_format(neuro_os::utils::LogLevel::ERROR, __FILE__, __LINE__, fmt, __VA_ARGS__)

}

#endif