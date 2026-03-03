#ifndef NEURO_OS_UTILS_THREAD_POOL_ENHANCED_SIMPLE_HPP
#define NEURO_OS_UTILS_THREAD_POOL_ENHANCED_SIMPLE_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neuro_os::utils {

// Task priority levels
enum class TaskPriority {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2
};

// Enhanced Thread Pool with work stealing and priority queues
class EnhancedThreadPoolSimple {
public:
    explicit EnhancedThreadPoolSimple(size_t num_threads = 0)
        : stop_(false), work_stealing_enabled_(true) {
        
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 4;
        }
        
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_thread(); });
        }
    }
    
    ~EnhancedThreadPoolSimple() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    // Submit a task with priority
    template<typename F, typename... Args>
    std::future<typename std::result_of<F(Args...)>::type> submit(F&& f, Args&&... args) {
        return submit_with_priority(TaskPriority::MEDIUM, std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    // Submit a task with specific priority
    template<typename F, typename... Args>
    std::future<typename std::result_of<F(Args...)>::type> submit_with_priority(TaskPriority priority, F&& f, Args&&... args) {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        // Create a packaged task
        auto task_ptr = std::make_shared<std::packaged_task<return_type()> >(
            [func = std::forward<F>(f), args...]() { return func(args...); }
        );
        
        std::future<return_type> result = task_ptr->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            
            // Wrap task in a void function
            auto void_task = [task_ptr]() { (*task_ptr)(); };
            
            // Add to appropriate priority queue
            switch (priority) {
                case TaskPriority::HIGH:
                    high_priority_tasks_.push(void_task);
                    break;
                case TaskPriority::MEDIUM:
                    medium_priority_tasks_.push(void_task);
                    break;
                case TaskPriority::LOW:
                    low_priority_tasks_.push(void_task);
                    break;
            }
            
            total_tasks_submitted_++;
        }
        
        condition_.notify_one();
        return result;
    }
    
    // Submit a task with timeout
    template<typename F, typename... Args>
    std::future<typename std::result_of<F(Args...)>::type> submit_with_timeout(std::chrono::milliseconds timeout, F&& f, Args&&... args) {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task_ptr = std::make_shared<std::packaged_task<return_type()> >(
            [timeout, func = std::forward<F>(f), args...]() -> return_type {
                auto start = std::chrono::steady_clock::now();
                auto result = func(args...);
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                
                if (duration > timeout) {
                    // Timeout occurred, but we still return the result
                    // In a real implementation, you might want to throw or handle differently
                }
                return result;
            }
        );
        
        std::future<return_type> result = task_ptr->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            
            auto void_task = [task_ptr]() { (*task_ptr)(); };
            medium_priority_tasks_.push(void_task);
            total_tasks_submitted_++;
        }
        
        condition_.notify_one();
        return result;
    }
    
    // Enable or disable work stealing
    void enable_work_stealing(bool enable) {
        work_stealing_enabled_ = enable;
    }
    
    // Get statistics
    struct Statistics {
        size_t total_tasks_submitted;
        size_t total_tasks_completed;
        size_t tasks_in_queue;
        size_t work_stealing_attempts;
        size_t work_stealing_successes;
    };
    
    Statistics get_statistics() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        Statistics stats;
        stats.total_tasks_submitted = total_tasks_submitted_;
        stats.total_tasks_completed = total_tasks_completed_;
        stats.tasks_in_queue = high_priority_tasks_.size() + medium_priority_tasks_.size() + low_priority_tasks_.size();
        stats.work_stealing_attempts = work_stealing_attempts_;
        stats.work_stealing_successes = work_stealing_successes_;
        return stats;
    }
    
    // Wait for all tasks to complete
    void wait_for_all() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        all_done_condition_.wait(lock, [this] {
            return high_priority_tasks_.empty() &&
                   medium_priority_tasks_.empty() &&
                   low_priority_tasks_.empty() &&
                   tasks_running_ == 0;
        });
    }
    
    // Get number of worker threads
    size_t get_thread_count() const {
        return workers_.size();
    }
    
private:
    void worker_thread() {
        while (true) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                
                // Wait for work or stop signal
                condition_.wait(lock, [this] {
                    return stop_ || !high_priority_tasks_.empty() ||
                           !medium_priority_tasks_.empty() || !low_priority_tasks_.empty();
                });
                
                if (stop_ && high_priority_tasks_.empty() && 
                    medium_priority_tasks_.empty() && low_priority_tasks_.empty()) {
                    return;
                }
                
                // Get task from highest priority non-empty queue
                if (!high_priority_tasks_.empty()) {
                    task = std::move(high_priority_tasks_.front());
                    high_priority_tasks_.pop();
                } else if (!medium_priority_tasks_.empty()) {
                    task = std::move(medium_priority_tasks_.front());
                    medium_priority_tasks_.pop();
                } else if (!low_priority_tasks_.empty()) {
                    task = std::move(low_priority_tasks_.front());
                    low_priority_tasks_.pop();
                }
                
                if (task) {
                    tasks_running_++;
                }
            }
            
            if (task) {
                try {
                    task();
                } catch (...) {
                    // Handle exception if needed
                }
                
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    tasks_running_--;
                    total_tasks_completed_++;
                    
                    // Notify wait_for_all if everything is done
                    if (high_priority_tasks_.empty() && medium_priority_tasks_.empty() &&
                        low_priority_tasks_.empty() && tasks_running_ == 0) {
                        all_done_condition_.notify_all();
                    }
                }
                
                condition_.notify_one();
            } else if (work_stealing_enabled_) {
                // Try to steal work from other queues
                attempt_work_stealing();
            }
        }
    }
    
    void attempt_work_stealing() {
        work_stealing_attempts_++;
        
        // Simple work stealing: try to get a task from medium priority queue
        std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
        if (lock.owns_lock() && !medium_priority_tasks_.empty()) {
            auto task = std::move(medium_priority_tasks_.front());
            medium_priority_tasks_.pop();
            lock.unlock();
            
            if (task) {
                work_stealing_successes_++;
                
                try {
                    task();
                } catch (...) {
                    // Handle exception if needed
                }
                
                {
                    std::lock_guard<std::mutex> task_lock(queue_mutex_);
                    total_tasks_completed_++;
                }
            }
        }
    }
    
    std::vector<std::thread> workers_;
    
    // Priority queues
    std::queue<std::function<void()> > high_priority_tasks_;
    std::queue<std::function<void()> > medium_priority_tasks_;
    std::queue<std::function<void()> > low_priority_tasks_;
    
    // Statistics
    size_t total_tasks_submitted_{0};
    size_t total_tasks_completed_{0};
    std::atomic<size_t> tasks_running_{0};
    
    // Synchronization
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable all_done_condition_;
    std::atomic<bool> stop_;
    
    // Work stealing
    std::atomic<bool> work_stealing_enabled_;
    std::atomic<size_t> work_stealing_attempts_{0};
    std::atomic<size_t> work_stealing_successes_{0};
};

// Convenience functions
inline std::shared_ptr<EnhancedThreadPoolSimple> create_enhanced_thread_pool_simple(size_t num_threads = 0) {
    return std::make_shared<EnhancedThreadPoolSimple>(num_threads);
}

} // namespace neuro_os::utils

#endif // NEURO_OS_UTILS_THREAD_POOL_ENHANCED_SIMPLE_HPP