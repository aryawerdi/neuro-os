#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <mutex>
#include <vector>
#include <functional>
#include <sstream>
#include <iostream>

namespace neuro_os {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logger {
public:
    Logger() : level_(LogLevel::INFO), output_(std::cout) {}
    explicit Logger(LogLevel level) : level_(level), output_(std::cout) {}
    
    void set_level(LogLevel level) { level_ = level; }
    LogLevel get_level() const { return level_; }
    
    void log(LogLevel level, std::string_view message) {
        if (level < level_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        output_ << "[" << level_to_string(level) << "] " << message << "\n";
    }
    
    void debug(std::string_view message) { log(LogLevel::DEBUG, message); }
    void info(std::string_view message) { log(LogLevel::INFO, message); }
    void warning(std::string_view message) { log(LogLevel::WARNING, message); }
    void error(std::string_view message) { log(LogLevel::ERROR, message); }
    void fatal(std::string_view message) { log(LogLevel::FATAL, message); }
    
    template<typename... Args>
    void logf(LogLevel level, Args&&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        log(level, oss.str());
    }
    
    template<typename... Args>
    void info_f(Args&&... args) { logf(LogLevel::INFO, std::forward<Args>(args)...); }
    
    template<typename... Args>
    void debug_f(Args&&... args) { logf(LogLevel::DEBUG, std::forward<Args>(args)...); }
    
    template<typename... Args>
    void warning_f(Args&&... args) { logf(LogLevel::WARNING, std::forward<Args>(args)...); }
    
    template<typename... Args>
    void error_f(Args&&... args) { logf(LogLevel::ERROR, std::forward<Args>(args)...); }
    
    template<typename... Args>
    void fatal_f(Args&&... args) { logf(LogLevel::FATAL, std::forward<Args>(args)...); }

private:
    static constexpr std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    LogLevel level_;
    std::ostream& output_;
    std::mutex mutex_;
};

class LogCategory {
public:
    explicit LogCategory(std::string name) : name_(std::move(name)) {}
    
    void debug(std::string_view msg) { logger_.debug("[" + name_ + "] " + std::string(msg)); }
    void info(std::string_view msg) { logger_.info("[" + name_ + "] " + std::string(msg)); }
    void warning(std::string_view msg) { logger_.warning("[" + name_ + "] " + std::string(msg)); }
    void error(std::string_view msg) { logger_.error("[" + name_ + "] " + std::string(msg)); }
    void fatal(std::string_view msg) { logger_.fatal("[" + name_ + "] " + std::string(msg)); }
    
    void set_level(LogLevel level) { logger_.set_level(level); }
    
private:
    Logger logger_;
    std::string name_;
};

}
