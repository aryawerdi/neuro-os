#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include <optional>

namespace neuro_os {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    template<typename F>
    auto submit(F&& task) -> std::future<std::invoke_result_t<F>> {
        using ReturnType = std::invoke_result_t<F>;
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<F>(task)
        );
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace([task_ptr]() { (*task_ptr)(); });
        }
        
        condition_.notify_one();
        return task_ptr->get_future();
    }
    
    void wait_all();
    void shutdown();
    size_t num_threads() const { return workers_.size(); }
    size_t active_tasks() const { return active_tasks_.load(); }
    size_t pending_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
    void resize(size_t new_size);
    void pause();
    void resume();
    bool is_paused() const { return paused_.load(); }

private:
    void worker_thread();
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    std::atomic<bool> paused_;
    std::atomic<size_t> active_tasks_;
};

class TaskScheduler {
public:
    TaskScheduler() : pool_(std::thread::hardware_concurrency()) {}
    explicit TaskScheduler(size_t threads) : pool_(threads) {}
    
    template<typename F>
    auto schedule(F&& task) -> std::future<std::invoke_result_t<F>> {
        return pool_.submit(std::forward<F>(task));
    }
    
    template<typename F, typename... Args>
    auto schedule_with_priority(int priority, F&& task, Args&&... args) -> std::future<std::invoke_result_t<F>> {
        auto bound_task = std::bind(std::forward<F>(task), std::forward<Args>(args)...);
        return pool_.submit([priority, task = std::move(bound_task)]() mutable {
            return task();
        });
    }
    
    void wait_all() { pool_.wait_all(); }
    size_t thread_count() const { return pool_.num_threads(); }
    
private:
    ThreadPool pool_;
};

}
