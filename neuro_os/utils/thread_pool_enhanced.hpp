#ifndef NEURO_OS_UTILS_THREAD_POOL_ENHANCED_HPP
#define NEURO_OS_UTILS_THREAD_POOL_ENHANCED_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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

// Task dependency graph node
struct TaskNode {
    std::function<void()> task;
    std::set<size_t> dependencies;
    std::set<size_t> dependents;
    TaskPriority priority;
    bool completed;
    
    TaskNode(std::function<void()> t, TaskPriority p = TaskPriority::MEDIUM)
        : task(std::move(t)), priority(p), completed(false) {}
};

// Enhanced Thread Pool with work stealing, priority queues, and task dependencies
class EnhancedThreadPool {
public:
    explicit EnhancedThreadPool(size_t num_threads = std::thread::hardware_concurrency())
        : stop_(false), work_stealing_enabled_(true) {
        
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
        }
        
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this, i] { worker_thread(i); });
        }
    }
    
    ~EnhancedThreadPool() {
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
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            
            size_t task_id = next_task_id_++;
            TaskNode node([task]() { (*task)(); }, priority);
            
            // Store in task map
            tasks_[task_id] = std::move(node);
            
            // Add to appropriate priority queue
            switch (priority) {
                case TaskPriority::HIGH:
                    high_priority_queue_.push(task_id);
                    break;
                case TaskPriority::MEDIUM:
                    medium_priority_queue_.push(task_id);
                    break;
                case TaskPriority::LOW:
                    low_priority_queue_.push(task_id);
                    break;
            }
        }
        
        condition_.notify_one();
        return result;
    }
    
    // Submit a task with dependencies
    template<typename F, typename... Args>
    auto submit_with_dependencies(const std::vector<size_t>& dependencies, F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            
            size_t task_id = next_task_id_++;
            TaskNode node([task]() { (*task)(); }, TaskPriority::MEDIUM);
            
            // Add dependencies
            for (size_t dep_id : dependencies) {
                if (tasks_.find(dep_id) != tasks_.end()) {
                    node.dependencies.insert(dep_id);
                    tasks_[dep_id].dependents.insert(task_id);
                }
            }
            
            // Store in task map
            tasks_[task_id] = std::move(node);
            
            // Add to pending tasks if no dependencies
            if (node.dependencies.empty()) {
                medium_priority_queue_.push(task_id);
            } else {
                pending_tasks_.insert(task_id);
            }
        }
        
        condition_.notify_one();
        return result;
    }
    
    // Submit a task with timeout
    template<typename F, typename... Args>
    auto submit_with_timeout(std::chrono::milliseconds timeout, F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [timeout, f = std::forward<F>(f), args...]() -> return_type {
                auto start = std::chrono::steady_clock::now();
                auto result = std::bind(f, args...)();
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                
                if (duration > timeout) {
                    // Log timeout or handle as needed
                    // For now, just return the result
                }
                return result;
            }
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            
            size_t task_id = next_task_id_++;
            TaskNode node([task]() { (*task)(); }, TaskPriority::MEDIUM);
            tasks_[task_id] = std::move(node);
            medium_priority_queue_.push(task_id);
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
        size_t tasks_with_dependencies;
        size_t work_stealing_attempts;
        size_t work_stealing_successes;
    };
    
    Statistics get_statistics() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        Statistics stats;
        stats.total_tasks_submitted = next_task_id_;
        stats.total_tasks_completed = completed_tasks_.size();
        stats.tasks_in_queue = high_priority_queue_.size() + medium_priority_queue_.size() + low_priority_queue_.size();
        stats.tasks_with_dependencies = pending_tasks_.size();
        stats.work_stealing_attempts = work_stealing_attempts_;
        stats.work_stealing_successes = work_stealing_successes_;
        return stats;
    }
    
    // Wait for all tasks to complete
    void wait_for_all() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        all_done_condition_.wait(lock, [this] {
            return high_priority_queue_.empty() &&
                   medium_priority_queue_.empty() &&
                   low_priority_queue_.empty() &&
                   pending_tasks_.empty() &&
                   tasks_running_ == 0;
        });
    }
    
    // Get number of worker threads
    size_t get_thread_count() const {
        return workers_.size();
    }
    
private:
    void worker_thread(size_t thread_id) {
        while (true) {
            std::function<void()> task;
            size_t task_id = 0;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                
                // Wait for work or stop signal
                condition_.wait(lock, [this] {
                    return stop_ || !high_priority_queue_.empty() ||
                           !medium_priority_queue_.empty() || !low_priority_queue_.empty();
                });
                
                if (stop_ && high_priority_queue_.empty() && 
                    medium_priority_queue_.empty() && low_priority_queue_.empty()) {
                    return;
                }
                
                // Get task from highest priority non-empty queue
                if (!high_priority_queue_.empty()) {
                    task_id = high_priority_queue_.front();
                    high_priority_queue_.pop();
                } else if (!medium_priority_queue_.empty()) {
                    task_id = medium_priority_queue_.front();
                    medium_priority_queue_.pop();
                } else if (!low_priority_queue_.empty()) {
                    task_id = low_priority_queue_.front();
                    low_priority_queue_.pop();
                }
                
                if (task_id != 0 && tasks_.find(task_id) != tasks_.end()) {
                    task = tasks_[task_id].task;
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
                    
                    // Mark task as completed
                    if (tasks_.find(task_id) != tasks_.end()) {
                        tasks_[task_id].completed = true;
                        completed_tasks_.insert(task_id);
                        
                        // Check dependent tasks
                        for (size_t dep_id : tasks_[task_id].dependents) {
                            if (tasks_.find(dep_id) != tasks_.end()) {
                                tasks_[dep_id].dependencies.erase(task_id);
                                if (tasks_[dep_id].dependencies.empty()) {
                                    // Move from pending to appropriate queue
                                    pending_tasks_.erase(dep_id);
                                    switch (tasks_[dep_id].priority) {
                                        case TaskPriority::HIGH:
                                            high_priority_queue_.push(dep_id);
                                            break;
                                        case TaskPriority::MEDIUM:
                                            medium_priority_queue_.push(dep_id);
                                            break;
                                        case TaskPriority::LOW:
                                            low_priority_queue_.push(dep_id);
                                            break;
                                    }
                                }
                            }
                        }
                        
                        // Clean up completed task
                        tasks_.erase(task_id);
                    }
                    
                    // Notify wait_for_all if everything is done
                    if (high_priority_queue_.empty() && medium_priority_queue_.empty() &&
                        low_priority_queue_.empty() && pending_tasks_.empty() && tasks_running_ == 0) {
                        all_done_condition_.notify_all();
                    }
                }
                
                condition_.notify_one();
            } else if (work_stealing_enabled_) {
                // Try to steal work from other queues
                attempt_work_stealing(thread_id);
            }
        }
    }
    
    void attempt_work_stealing(size_t thief_thread_id) {
        work_stealing_attempts_++;
        
        // Simple work stealing: try to get a task from medium priority queue
        std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
        if (lock.owns_lock() && !medium_priority_queue_.empty()) {
            size_t task_id = medium_priority_queue_.front();
            medium_priority_queue_.pop();
            lock.unlock();
            
            if (tasks_.find(task_id) != tasks_.end()) {
                auto task = tasks_[task_id].task;
                work_stealing_successes_++;
                
                try {
                    task();
                } catch (...) {
                    // Handle exception if needed
                }
                
                {
                    std::lock_guard<std::mutex> task_lock(queue_mutex_);
                    tasks_.erase(task_id);
                    completed_tasks_.insert(task_id);
                }
            }
        }
    }
    
    std::vector<std::thread> workers_;
    
    // Priority queues
    std::queue<size_t> high_priority_queue_;
    std::queue<size_t> medium_priority_queue_;
    std::queue<size_t> low_priority_queue_;
    
    // Task management
    std::map<size_t, TaskNode> tasks_;
    std::set<size_t> pending_tasks_;
    std::set<size_t> completed_tasks_;
    size_t next_task_id_{1};
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
inline std::shared_ptr<EnhancedThreadPool> create_enhanced_thread_pool(size_t num_threads = 0) {
    return std::make_shared<EnhancedThreadPool>(num_threads);
}

} // namespace neuro_os::utils

#endif // NEURO_OS_UTILS_THREAD_POOL_ENHANCED_HPP