#ifndef NEURO_OS_UTILS_THREAD_POOL_HPP
#define NEURO_OS_UTILS_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace neuro_os::utils {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    template<typename F>
    auto enqueue(F&& task) -> std::future<std::invoke_result_t<F>> {
        using return_type = std::invoke_result_t<F>;
        auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(task)
        );
        
        std::future<return_type> result = task_ptr->get_future();
        
        {
            std::unique_lock lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("Cannot enqueue on stopped ThreadPool");
            }
            tasks_.emplace([task_ptr]() { (*task_ptr)(); });
        }
        
        condition_.notify_one();
        return result;
    }

    void wait_all();

    size_t num_threads() const { return workers_.size(); }
    size_t pending_tasks() const;
    bool is_running() const { return !stop_; }

    void shutdown();
    void wait_and_shutdown();

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
};

inline ThreadPool::ThreadPool(size_t num_threads) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

inline ThreadPool::~ThreadPool() {
    shutdown();
}

inline void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex_);
            condition_.wait(lock, [this] {
                return stop_.load() || !tasks_.empty();
            });
            
            if (stop_.load() && tasks_.empty()) {
                return;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            active_tasks_.fetch_add(1, std::memory_order_relaxed);
            task();
            active_tasks_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

inline size_t ThreadPool::pending_tasks() const {
    std::unique_lock lock(const_cast<std::mutex&>(queue_mutex_));
    return tasks_.size() + active_tasks_.load(std::memory_order_relaxed);
}

inline void ThreadPool::wait_all() {
    while (pending_tasks() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

inline void ThreadPool::shutdown() {
    stop_.store(true);
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

inline void ThreadPool::wait_and_shutdown() {
    wait_all();
    shutdown();
}

class ThreadPoolManager {
public:
    static ThreadPoolManager& instance() {
        static ThreadPoolManager instance;
        return instance;
    }

    ThreadPool& get_pool(size_t id = 0) {
        std::unique_lock lock(mutex_);
        if (pools_.find(id) == pools_.end()) {
            pools_[id] = std::make_unique<ThreadPool>();
        }
        return *pools_[id];
    }

    void shutdown_all() {
        std::unique_lock lock(mutex_);
        for (auto& [id, pool] : pools_) {
            pool->shutdown();
        }
        pools_.clear();
    }

private:
    ThreadPoolManager() = default;
    ~ThreadPoolManager() {
        shutdown_all();
    }

    ThreadPoolManager(const ThreadPoolManager&) = delete;
    ThreadPoolManager& operator=(const ThreadPoolManager&) = delete;

    std::mutex mutex_;
    std::map<size_t, std::unique_ptr<ThreadPool>> pools_;
};

}

#endif
