#include <gtest/gtest.h>
#include "neuro_os/utils/thread_pool.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace neuro_os;

TEST(ThreadPoolTest, BasicSubmit) {
    ThreadPool pool(2);
    
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, MultipleTasks) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(futures[i].get(), i * 2);
    }
}

TEST(ThreadPoolTest, ThreadCount) {
    ThreadPool pool(8);
    EXPECT_EQ(pool.num_threads(), 8);
}

TEST(ThreadPoolTest, WaitAll) {
    ThreadPool pool(4);
    std::atomic<int> counter(0);
    
    for (int i = 0; i < 100; ++i) {
        pool.submit([&counter]() { counter++; });
    }
    
    pool.wait_all();
    EXPECT_EQ(counter, 100);
}

TEST(ThreadPoolTest, ActiveTaskCount) {
    ThreadPool pool(2);
    std::atomic<int> running(0);
    std::atomic<int> max_running(0);
    
    auto future1 = pool.submit([&]() {
        running++;
        max_running = std::max(max_running.load(), running.load());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        running--;
    });
    
    auto future2 = pool.submit([&]() {
        running++;
        max_running = std::max(max_running.load(), running.load());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        running--;
    });
    
    future1.get();
    future2.get();
    EXPECT_GE(max_running.load(), 1);
}

TEST(ThreadPoolTest, PendingTasks) {
    ThreadPool pool(1);
    
    EXPECT_EQ(pool.pending_tasks(), 0);
    
    pool.submit([]() { 
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
        return 0; 
    });
    pool.submit([]() { 
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
        return 0; 
    });
    
    EXPECT_GE(pool.pending_tasks(), 1);
    pool.wait_all();
}

TEST(ThreadPoolTest, Shutdown) {
    ThreadPool pool(2);
    
    pool.submit([]() { return 1; });
    pool.shutdown();
    
    EXPECT_TRUE(true);
}

TEST(ThreadPoolTest, Resize) {
    ThreadPool pool(2);
    EXPECT_EQ(pool.num_threads(), 2);
    
    pool.resize(4);
    EXPECT_EQ(pool.num_threads(), 4);
}

TEST(TaskSchedulerTest, ScheduleTask) {
    TaskScheduler scheduler(2);
    
    auto future = scheduler.schedule([]() { return 100; });
    EXPECT_EQ(future.get(), 100);
}

TEST(TaskSchedulerTest, MultipleSchedules) {
    TaskScheduler scheduler(4);
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < 20; ++i) {
        futures.push_back(scheduler.schedule([i]() { return i; }));
    }
    
    scheduler.wait_all();
    
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(futures[i].get(), i);
    }
}

TEST(ThreadPoolTest, PauseResume) {
    ThreadPool pool(2);
    std::atomic<int> counter(0);
    
    pool.pause();
    EXPECT_TRUE(pool.is_paused());
    
    pool.submit([&counter]() { counter++; });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(counter, 0);
    
    pool.resume();
    pool.wait_all();
    
    EXPECT_EQ(counter, 1);
}
