#ifndef NEURO_OS_DISTRIBUTED_REPLICATED_STATE_HPP
#define NEURO_OS_DISTRIBUTED_REPLICATED_STATE_HPP

#ifndef NEURO_OS_DISTRIBUTED_OPERATION_DEFINED
#define NEURO_OS_DISTRIBUTED_OPERATION_DEFINED
#endif

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <string>
#include <functional>
#include <cstring>
#include <chrono>

namespace neuro_os::distributed {

struct Operation {
    uint64_t client_id;
    uint64_t request_id;
    std::vector<uint8_t> data;
    std::chrono::steady_clock::time_point timestamp;
    
    Operation() : client_id(0), request_id(0), timestamp(std::chrono::steady_clock::now()) {}
    
    bool operator==(const Operation& other) const {
        return client_id == other.client_id && request_id == other.request_id;
    }
};

class ReplicatedStateMachine {
private:
    std::vector<Operation> operation_log_;
    uint64_t applied_index_ = 0;
    std::vector<uint8_t> state_;
    mutable std::mutex mutex_;
    
    std::function<void(const Operation&)> state_applier_;
    
public:
    ReplicatedStateMachine();
    ~ReplicatedStateMachine() = default;
    
    void apply(const Operation& op);
    
    std::vector<Operation> get_log(size_t from, size_t to) const;
    std::vector<uint8_t> get_snapshot() const;
    void load_snapshot(const std::vector<uint8_t>& snapshot);
    
    uint64_t applied_index() const { return applied_index_; }
    size_t log_size() const { return operation_log_.size(); }
    
    void set_applier(std::function<void(const Operation&)> applier);
    
    const std::vector<uint8_t>& state() const { return state_; }
    void set_state(const std::vector<uint8_t>& state);
};

inline ReplicatedStateMachine::ReplicatedStateMachine() {
    state_.reserve(4096);
}

inline void ReplicatedStateMachine::apply(const Operation& op) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    operation_log_.push_back(op);
    
    if (state_applier_) {
        state_applier_(op);
    } else {
        state_.insert(state_.end(), op.data.begin(), op.data.end());
    }
    
    ++applied_index_;
}

inline std::vector<Operation> ReplicatedStateMachine::get_log(size_t from, size_t to) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Operation> result;
    
    if (from >= operation_log_.size()) {
        return result;
    }
    
    size_t end = std::min(to, operation_log_.size());
    
    for (size_t i = from; i < end; ++i) {
        result.push_back(operation_log_[i]);
    }
    
    return result;
}

inline std::vector<uint8_t> ReplicatedStateMachine::get_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint8_t> snapshot;
    
    uint64_t index = applied_index_;
    auto index_bytes = reinterpret_cast<const uint8_t*>(&index);
    snapshot.insert(snapshot.end(), index_bytes, index_bytes + sizeof(uint64_t));
    
    size_t state_size = state_.size();
    auto size_bytes = reinterpret_cast<const uint8_t*>(&state_size);
    snapshot.insert(snapshot.end(), size_bytes, size_bytes + sizeof(size_t));
    
    snapshot.insert(snapshot.end(), state_.begin(), state_.end());
    
    for (size_t i = 0; i < operation_log_.size() && i < 100; ++i) {
        uint64_t client_id = operation_log_[i].client_id;
        auto client_bytes = reinterpret_cast<const uint8_t*>(&client_id);
        snapshot.insert(snapshot.end(), client_bytes, client_bytes + sizeof(uint64_t));
        
        uint64_t request_id = operation_log_[i].request_id;
        auto req_bytes = reinterpret_cast<const uint8_t*>(&request_id);
        snapshot.insert(snapshot.end(), req_bytes, req_bytes + sizeof(uint64_t));
        
        size_t data_size = operation_log_[i].data.size();
        auto op_size_bytes = reinterpret_cast<const uint8_t*>(&data_size);
        snapshot.insert(snapshot.end(), op_size_bytes, op_size_bytes + sizeof(size_t));
        
        snapshot.insert(snapshot.end(), operation_log_[i].data.begin(), operation_log_[i].data.end());
    }
    
    return snapshot;
}

inline void ReplicatedStateMachine::load_snapshot(const std::vector<uint8_t>& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (snapshot.size() < sizeof(uint64_t) + sizeof(size_t)) {
        return;
    }
    
    size_t offset = 0;
    
    uint64_t index = 0;
    std::memcpy(&index, snapshot.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    size_t state_size = 0;
    std::memcpy(&state_size, snapshot.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);
    
    state_.clear();
    if (state_size > 0 && offset + state_size <= snapshot.size()) {
        state_.assign(snapshot.begin() + offset, snapshot.begin() + offset + state_size);
        offset += state_size;
    }
    
    applied_index_ = index;
    
    operation_log_.clear();
    while (offset + sizeof(uint64_t) + sizeof(uint64_t) + sizeof(size_t) <= snapshot.size()) {
        Operation op;
        
        std::memcpy(&op.client_id, snapshot.data() + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        
        std::memcpy(&op.request_id, snapshot.data() + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        
        size_t data_size = 0;
        std::memcpy(&data_size, snapshot.data() + offset, sizeof(size_t));
        offset += sizeof(size_t);
        
        if (offset + data_size <= snapshot.size()) {
            op.data.assign(snapshot.begin() + offset, snapshot.begin() + offset + data_size);
            offset += data_size;
        }
        
        operation_log_.push_back(op);
    }
}

inline void ReplicatedStateMachine::set_applier(std::function<void(const Operation&)> applier) {
    state_applier_ = std::move(applier);
}

inline void ReplicatedStateMachine::set_state(const std::vector<uint8_t>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
}

}

#endif
