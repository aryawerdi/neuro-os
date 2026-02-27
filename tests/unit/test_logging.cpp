#include <gtest/gtest.h>
#include "neuro_os/utils/logging.hpp"
#include <sstream>
#include <thread>
#include <vector>

using namespace neuro_os;

TEST(LoggingTest, BasicLogging) {
    Logger logger;
    logger.set_level(LogLevel::DEBUG);
    
    testing::internal::CaptureStdout();
    logger.info("Test message");
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("[INFO]") != std::string::npos);
    EXPECT_TRUE(output.find("Test message") != std::string::npos);
}

TEST(LoggingTest, LogLevels) {
    Logger logger;
    logger.set_level(LogLevel::WARNING);
    
    testing::internal::CaptureStdout();
    logger.debug("Debug message");
    logger.info("Info message");
    logger.warning("Warning message");
    logger.error("Error message");
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("Debug message") == std::string::npos);
    EXPECT_TRUE(output.find("Info message") == std::string::npos);
    EXPECT_TRUE(output.find("Warning message") != std::string::npos);
    EXPECT_TRUE(output.find("Error message") != std::string::npos);
}

TEST(LoggingTest, LogLevelFiltering) {
    Logger logger;
    
    logger.set_level(LogLevel::ERROR);
    testing::internal::CaptureStdout();
    logger.debug("should not appear");
    logger.info("should not appear");
    logger.warning("should not appear");
    logger.error("should appear");
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("should appear") != std::string::npos);
    EXPECT_TRUE(output.find("should not appear") == std::string::npos);
}

TEST(LoggingTest, FormattedLogging) {
    Logger logger;
    logger.set_level(LogLevel::INFO);
    
    testing::internal::CaptureStdout();
    logger.info_f("Value: ", 42, " String: ", "test");
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("Value: 42") != std::string::npos);
    EXPECT_TRUE(output.find("String: test") != std::string::npos);
}

TEST(LoggingTest, LogCategory) {
    LogCategory category("TestCategory");
    category.set_level(LogLevel::DEBUG);
    
    testing::internal::CaptureStdout();
    category.info("Test message");
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("[TestCategory]") != std::string::npos);
    EXPECT_TRUE(output.find("Test message") != std::string::npos);
}

TEST(LoggingTest, ConcurrentLogging) {
    Logger logger;
    logger.set_level(LogLevel::DEBUG);
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&logger, i]() {
            for (int j = 0; j < 100; ++j) {
                logger.info("Message " + std::to_string(i) + " " + std::to_string(j));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    SUCCEED();
}

TEST(LoggingTest, LogLevelEnum) {
    EXPECT_EQ(static_cast<int>(LogLevel::DEBUG), 0);
    EXPECT_EQ(static_cast<int>(LogLevel::INFO), 1);
    EXPECT_EQ(static_cast<int>(LogLevel::WARNING), 2);
    EXPECT_EQ(static_cast<int>(LogLevel::ERROR), 3);
    EXPECT_EQ(static_cast<int>(LogLevel::FATAL), 4);
}
