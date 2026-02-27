#ifndef NEURO_OS_DISTRIBUTED_PBFT_CONSENSUS_HPP
#define NEURO_OS_DISTRIBUTED_PBFT_CONSENSUS_HPP

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <chrono>
#include <functional>
#include <algorithm>
#include <mutex>
#include <thread>
#include <cstring>
#include <string>

namespace neuro_os::distributed {

class ReplicatedStateMachine;

struct PBFTConfig {
    size_t replica_count;
    size_t byzantine_tolerance;
    std::chrono::milliseconds request_timeout;
    std::chrono::milliseconds view_change_timeout;
    
    PBFTConfig() 
        : replica_count(0)
        , byzantine_tolerance(0)
        , request_timeout(1000)
        , view_change_timeout(5000) {}
    
    bool is_valid() const {
        return replica_count >= 3 * byzantine_tolerance + 1;
    }
};

enum class MessageType : uint8_t {
    PrePrepare = 0,
    Prepare = 1,
    Commit = 2,
    ViewChange = 3,
    NewView = 4,
    Checkpoint = 5,
    Request = 6
};

#ifndef NEURO_OS_DISTRIBUTED_OPERATION_DEFINED
#define NEURO_OS_DISTRIBUTED_OPERATION_DEFINED
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
#endif

struct PrePrepare {
    uint64_t view_number;
    uint64_t sequence_number;
    Operation operation;
    std::vector<uint8_t> digest;
    
    PrePrepare() : view_number(0), sequence_number(0) {}
};

struct Prepare {
    uint64_t view_number;
    uint64_t sequence_number;
    std::vector<uint8_t> digest;
    size_t replica_id;
    
    Prepare() : view_number(0), sequence_number(0), replica_id(0) {}
};

struct Commit {
    uint64_t view_number;
    uint64_t sequence_number;
    std::vector<uint8_t> digest;
    size_t replica_id;
    
    Commit() : view_number(0), sequence_number(0), replica_id(0) {}
};

struct Checkpoint {
    uint64_t sequence_number;
    std::vector<uint8_t> state_digest;
    std::vector<uint8_t> state_data;
    size_t replica_id;
    
    Checkpoint() : sequence_number(0), replica_id(0) {}
};

struct ViewChange {
    uint64_t view_number;
    uint64_t checkpoint_sequence;
    std::vector<Checkpoint> checkpoints;
    std::vector<PrePrepare> prepared;
    size_t replica_id;
    
    ViewChange() : view_number(0), checkpoint_sequence(0), replica_id(0) {}
};

struct NewView {
    uint64_t view_number;
    uint64_t view_change_sequence;
    std::vector<PrePrepare> pre_prepares;
    std::vector<ViewChange> view_changes;
    
    NewView() : view_number(0), view_change_sequence(0) {}
};

struct ConsensusResult {
    bool success;
    std::vector<uint8_t> result;
    std::string error_msg;
    
    ConsensusResult() : success(false) {}
    
    static ConsensusResult ok(const std::vector<uint8_t>& result) {
        ConsensusResult r;
        r.success = true;
        r.result = result;
        return r;
    }
    
    static ConsensusResult make_error(const std::string& err) {
        ConsensusResult r;
        r.success = false;
        r.error_msg = err;
        return r;
    }
};

class PBFTConsensus {
public:
    using MessageHandler = std::function<void(size_t, const std::vector<uint8_t>&)>;
    
private:
    PBFTConfig config_;
    size_t replica_id_;
    uint64_t current_view_ = 0;
    uint64_t sequence_number_ = 0;
    uint64_t low_water_mark_ = 0;
    uint64_t high_water_mark_ = 100;
    
    enum class State {
        Normal,
        ViewChanging,
        Recovering
    };
    
    State current_state_ = State::Normal;
    
    struct PendingRequest {
        Operation operation;
        std::chrono::steady_clock::time_point start_time;
        bool completed = false;
        std::vector<uint8_t> result;
    };
    
    struct RequestEntry {
        PrePrepare pre_prepare;
        std::unordered_set<size_t> prepares;
        std::unordered_set<size_t> commits;
        bool committed = false;
        bool executed = false;
    };
    
    std::unordered_map<uint64_t, PendingRequest> pending_requests_;
    std::unordered_map<uint64_t, RequestEntry> request_log_;
    std::unordered_map<uint64_t, ViewChange> view_change_messages_;
    std::unordered_set<size_t> faulty_replicas_;
    
    std::mutex mutex_;
    MessageHandler message_sender_;
    ReplicatedStateMachine* state_machine_ = nullptr;
    
    bool is_primary() const;
    size_t get_primary(uint64_t view) const;
    bool validate_digest(const std::vector<uint8_t>& digest, const Operation& op) const;
    std::vector<uint8_t> compute_digest(const Operation& op) const;
    
    void send_pre_prepare(const PrePrepare& msg);
    void send_prepare(const PrePrepare& pre_prepare);
    void send_commit(const PrePrepare& pre_prepare);
    void broadcast_message(MessageType type, const std::vector<uint8_t>& data);
    
    void execute_request(const RequestEntry& entry);
    bool has_quorum_prepare(const RequestEntry& entry) const;
    bool has_quorum_commit(const RequestEntry& entry) const;
    
    void checkpoint_state();
    void gc_request_log(uint64_t sequence);
    
public:
    PBFTConsensus();
    ~PBFTConsensus() = default;
    
    void initialize(const PBFTConfig& config, size_t replica_id);
    void set_message_sender(MessageHandler handler);
    void set_state_machine(ReplicatedStateMachine* sm);
    
    template<typename T>
    ConsensusResult propose(const T& value);
    
    void handle_request(const Operation& op);
    void on_pre_prepare(const PrePrepare& msg);
    void on_prepare(const Prepare& msg);
    void on_commit(const Commit& msg);
    void on_checkpoint(const Checkpoint& msg);
    void handle_view_change(const ViewChange& msg);
    void on_new_view(const NewView& msg);
    
    void initiate_view_change();
    
    uint64_t current_view() const { return current_view_; }
    uint64_t sequence_number() const { return sequence_number_; }
    size_t replica_id() const { return replica_id_; }
    const PBFTConfig& config() const { return config_; }
    
    bool primary() const { return is_primary(); }
    State state() const { return current_state_; }
    
    std::vector<PrePrepare> get_prepared_requests() const;
    Checkpoint get_stable_checkpoint() const;
};

#ifndef NEURO_OS_DISTRIBUTED_REPLICATED_STATE_HPP
#include "replicated_state.hpp"
#endif

inline PBFTConsensus::PBFTConsensus() {
    faulty_replicas_.reserve(64);
}

inline void PBFTConsensus::initialize(const PBFTConfig& config, size_t replica_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!config.is_valid()) {
        throw std::invalid_argument("Invalid PBFT config: n >= 3f + 1 required");
    }
    
    config_ = config;
    replica_id_ = replica_id;
    current_view_ = 0;
    sequence_number_ = 0;
    low_water_mark_ = 0;
    current_state_ = State::Normal;
}

inline void PBFTConsensus::set_message_sender(MessageHandler handler) {
    message_sender_ = std::move(handler);
}

inline void PBFTConsensus::set_state_machine(ReplicatedStateMachine* sm) {
    state_machine_ = sm;
}

inline bool PBFTConsensus::is_primary() const {
    return get_primary(current_view_) == replica_id_;
}

inline size_t PBFTConsensus::get_primary(uint64_t view) const {
    return view % config_.replica_count;
}

inline std::vector<uint8_t> PBFTConsensus::compute_digest(const Operation& op) const {
    std::vector<uint8_t> digest;
    digest.reserve(32);
    
    uint64_t hash = op.client_id ^ op.request_id;
    for (size_t i = 0; i < op.data.size(); ++i) {
        hash = hash * 31 + op.data[i];
    }
    
    for (int i = 0; i < 8; ++i) {
        digest.push_back(static_cast<uint8_t>((hash >> (i * 8)) & 0xFF));
    }
    
    for (int i = 0; i < 8; ++i) {
        digest.push_back(static_cast<uint8_t>((hash >> ((i + 8) * 8)) & 0xFF));
    }
    
    return digest;
}

inline bool PBFTConsensus::validate_digest(const std::vector<uint8_t>& digest, const Operation& op) const {
    auto computed = compute_digest(op);
    return digest == computed;
}

inline void PBFTConsensus::broadcast_message(MessageType type, const std::vector<uint8_t>& data) {
    if (message_sender_) {
        for (size_t i = 0; i < config_.replica_count; ++i) {
            if (i != replica_id_) {
                message_sender_(i, data);
            }
        }
    }
}

template<typename T>
ConsensusResult PBFTConsensus::propose(const T& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (current_state_ != State::Normal) {
        return ConsensusResult::make_error("Not in normal state");
    }
    
    if (!is_primary()) {
        return ConsensusResult::make_error("Not primary replica");
    }
    
    Operation op;
    op.data.resize(sizeof(T));
    std::memcpy(op.data.data(), &value, sizeof(T));
    op.request_id = ++sequence_number_;
    op.timestamp = std::chrono::steady_clock::now();
    
    PrePrepare pre_prepare;
    pre_prepare.view_number = current_view_;
    pre_prepare.sequence_number = sequence_number_;
    pre_prepare.operation = op;
    pre_prepare.digest = compute_digest(op);
    
    {
        RequestEntry entry;
        entry.pre_prepare = pre_prepare;
        request_log_[sequence_number_] = std::move(entry);
    }
    
    send_pre_prepare(pre_prepare);
    
    PendingRequest pending;
    pending.operation = op;
    pending.start_time = std::chrono::steady_clock::now();
    pending_requests_[op.request_id] = std::move(pending);
    
    lock.unlock();
    
    auto timeout = config_.request_timeout;
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        {
            std::lock_guard<std::mutex> lock2(mutex_);
            auto it = pending_requests_.find(op.request_id);
            if (it != pending_requests_.end() && it->second.completed) {
                return ConsensusResult::ok(it->second.result);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return ConsensusResult::make_error("Request timeout");
}

inline void PBFTConsensus::handle_request(const Operation& op) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (is_primary()) {
        PrePrepare pre_prepare;
        pre_prepare.view_number = current_view_;
        pre_prepare.sequence_number = ++sequence_number_;
        pre_prepare.operation = op;
        pre_prepare.digest = compute_digest(op);
        
        {
            RequestEntry entry;
            entry.pre_prepare = pre_prepare;
            request_log_[sequence_number_] = std::move(entry);
        }
        
        send_pre_prepare(pre_prepare);
        
        PendingRequest pending;
        pending.operation = op;
        pending.start_time = std::chrono::steady_clock::now();
        pending_requests_[op.request_id] = std::move(pending);
    } else {
        lock.unlock();
        initiate_view_change();
    }
}

inline void PBFTConsensus::send_pre_prepare(const PrePrepare& msg) {
    if (message_sender_) {
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(MessageType::PrePrepare));
        
        auto view_bytes = reinterpret_cast<const uint8_t*>(&msg.view_number);
        data.insert(data.end(), view_bytes, view_bytes + sizeof(uint64_t));
        
        auto seq_bytes = reinterpret_cast<const uint8_t*>(&msg.sequence_number);
        data.insert(data.end(), seq_bytes, seq_bytes + sizeof(uint64_t));
        
        data.insert(data.end(), msg.digest.begin(), msg.digest.end());
        
        for (size_t i = 0; i < config_.replica_count; ++i) {
            if (i != replica_id_) {
                message_sender_(i, data);
            }
        }
    }
}

inline void PBFTConsensus::send_prepare(const PrePrepare& pre_prepare) {
    if (message_sender_) {
        Prepare prepare;
        prepare.view_number = pre_prepare.view_number;
        prepare.sequence_number = pre_prepare.sequence_number;
        prepare.digest = pre_prepare.digest;
        prepare.replica_id = replica_id_;
        
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(MessageType::Prepare));
        
        auto view_bytes = reinterpret_cast<const uint8_t*>(&prepare.view_number);
        data.insert(data.end(), view_bytes, view_bytes + sizeof(uint64_t));
        
        auto seq_bytes = reinterpret_cast<const uint8_t*>(&prepare.sequence_number);
        data.insert(data.end(), seq_bytes, seq_bytes + sizeof(uint64_t));
        
        data.insert(data.end(), prepare.digest.begin(), prepare.digest.end());
        
        auto rep_bytes = reinterpret_cast<const uint8_t*>(&prepare.replica_id);
        data.insert(data.end(), rep_bytes, rep_bytes + sizeof(size_t));
        
        broadcast_message(MessageType::Prepare, data);
    }
}

inline void PBFTConsensus::send_commit(const PrePrepare& pre_prepare) {
    if (message_sender_) {
        Commit commit;
        commit.view_number = pre_prepare.view_number;
        commit.sequence_number = pre_prepare.sequence_number;
        commit.digest = pre_prepare.digest;
        commit.replica_id = replica_id_;
        
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(MessageType::Commit));
        
        auto view_bytes = reinterpret_cast<const uint8_t*>(&commit.view_number);
        data.insert(data.end(), view_bytes, view_bytes + sizeof(uint64_t));
        
        auto seq_bytes = reinterpret_cast<const uint8_t*>(&commit.sequence_number);
        data.insert(data.end(), seq_bytes, seq_bytes + sizeof(uint64_t));
        
        data.insert(data.end(), commit.digest.begin(), commit.digest.end());
        
        auto rep_bytes = reinterpret_cast<const uint8_t*>(&commit.replica_id);
        data.insert(data.end(), rep_bytes, rep_bytes + sizeof(size_t));
        
        broadcast_message(MessageType::Commit, data);
    }
}

inline void PBFTConsensus::on_pre_prepare(const PrePrepare& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (current_state_ != State::Normal) {
        return;
    }
    
    if (msg.view_number != current_view_) {
        return;
    }
    
    if (msg.sequence_number < low_water_mark_ || msg.sequence_number > high_water_mark_) {
        return;
    }
    
    if (!validate_digest(msg.digest, msg.operation)) {
        return;
    }
    
    auto& entry = request_log_[msg.sequence_number];
    entry.pre_prepare = msg;
    
    lock.unlock();
    send_prepare(msg);
}

inline void PBFTConsensus::on_prepare(const Prepare& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (msg.view_number != current_view_) {
        return;
    }
    
    if (msg.sequence_number < low_water_mark_ || msg.sequence_number > high_water_mark_) {
        return;
    }
    
    auto it = request_log_.find(msg.sequence_number);
    if (it == request_log_.end()) {
        return;
    }
    
    if (it->second.pre_prepare.digest != msg.digest) {
        return;
    }
    
    it->second.prepares.insert(msg.replica_id);
    
    if (!it->second.committed && has_quorum_prepare(it->second)) {
        it->second.committed = true;
        auto pre_prepare = it->second.pre_prepare;
        lock.unlock();
        send_commit(pre_prepare);
    }
}

inline void PBFTConsensus::on_commit(const Commit& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (msg.view_number != current_view_) {
        return;
    }
    
    if (msg.sequence_number < low_water_mark_ || msg.sequence_number > high_water_mark_) {
        return;
    }
    
    auto it = request_log_.find(msg.sequence_number);
    if (it == request_log_.end()) {
        return;
    }
    
    if (it->second.pre_prepare.digest != msg.digest) {
        return;
    }
    
    it->second.commits.insert(msg.replica_id);
    
    if (!it->second.executed && has_quorum_commit(it->second)) {
        it->second.executed = true;
        execute_request(it->second);
    }
}

inline bool PBFTConsensus::has_quorum_prepare(const RequestEntry& entry) const {
    size_t total_valid = 1 + entry.prepares.size();
    return total_valid >= 2 * config_.byzantine_tolerance + 1;
}

inline bool PBFTConsensus::has_quorum_commit(const RequestEntry& entry) const {
    size_t total_valid = 1 + entry.commits.size();
    return total_valid >= 2 * config_.byzantine_tolerance + 1;
}

inline void PBFTConsensus::execute_request(const RequestEntry& entry) {
    if (state_machine_) {
        state_machine_->apply(entry.pre_prepare.operation);
    }
    
    auto req_it = pending_requests_.find(entry.pre_prepare.operation.request_id);
    if (req_it != pending_requests_.end()) {
        req_it->second.completed = true;
        req_it->second.result = entry.pre_prepare.operation.data;
    }
    
    if (entry.pre_prepare.sequence_number % 100 == 0) {
        checkpoint_state();
    }
    
    gc_request_log(entry.pre_prepare.sequence_number);
}

inline void PBFTConsensus::on_checkpoint(const Checkpoint& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (msg.sequence_number > low_water_mark_) {
        checkpoint_state();
    }
}

inline void PBFTConsensus::checkpoint_state() {
    if (!state_machine_) return;
    
    Checkpoint checkpoint;
    checkpoint.sequence_number = sequence_number_;
    checkpoint.state_digest = state_machine_->get_snapshot();
    checkpoint.replica_id = replica_id_;
    
    if (message_sender_) {
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(MessageType::Checkpoint));
        
        auto seq_bytes = reinterpret_cast<const uint8_t*>(&checkpoint.sequence_number);
        data.insert(data.end(), seq_bytes, seq_bytes + sizeof(uint64_t));
        
        data.insert(data.end(), checkpoint.state_digest.begin(), checkpoint.state_digest.end());
        
        auto rep_bytes = reinterpret_cast<const uint8_t*>(&checkpoint.replica_id);
        data.insert(data.end(), rep_bytes, rep_bytes + sizeof(size_t));
        
        broadcast_message(MessageType::Checkpoint, data);
    }
}

inline void PBFTConsensus::gc_request_log(uint64_t sequence) {
    low_water_mark_ = sequence;
    
    for (auto it = request_log_.begin(); it != request_log_.end();) {
        if (it->first < low_water_mark_) {
            it = request_log_.erase(it);
        } else {
            ++it;
        }
    }
}

inline void PBFTConsensus::handle_view_change(const ViewChange& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (msg.view_number <= current_view_) {
        return;
    }
    
    view_change_messages_[msg.replica_id] = msg;
    
    size_t stable_count = 0;
    for (const auto& vc_pair : view_change_messages_) {
        if (vc_pair.second.view_number == msg.view_number) {
            ++stable_count;
        }
    }
    
    if (stable_count >= config_.byzantine_tolerance + 1 && is_primary()) {
        NewView new_view;
        new_view.view_number = msg.view_number;
        new_view.view_changes.reserve(stable_count);
        
        for (const auto& vc_pair : view_change_messages_) {
            if (vc_pair.second.view_number == msg.view_number) {
                new_view.view_changes.push_back(vc_pair.second);
            }
        }
        
        uint64_t min_seq = UINT64_MAX;
        for (const auto& vc : new_view.view_changes) {
            if (vc.checkpoint_sequence < min_seq) {
                min_seq = vc.checkpoint_sequence;
            }
        }
        
        for (const auto& vc : new_view.view_changes) {
            for (const auto& pp : vc.prepared) {
                if (pp.sequence_number > min_seq) {
                    new_view.pre_prepares.push_back(pp);
                }
            }
        }
        
        current_view_ = msg.view_number;
        sequence_number_ = min_seq;
        current_state_ = State::Normal;
        
        lock.unlock();
        
        if (message_sender_) {
            std::vector<uint8_t> data;
            data.push_back(static_cast<uint8_t>(MessageType::NewView));
            
            auto view_bytes = reinterpret_cast<const uint8_t*>(&new_view.view_number);
            data.insert(data.end(), view_bytes, view_bytes + sizeof(uint64_t));
            
            broadcast_message(MessageType::NewView, data);
        }
    }
}

inline void PBFTConsensus::on_new_view(const NewView& msg) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (msg.view_number <= current_view_) {
        return;
    }
    
    current_view_ = msg.view_number;
    current_state_ = State::Normal;
    
    uint64_t min_seq = 0;
    for (const auto& pp : msg.pre_prepares) {
        if (pp.sequence_number > min_seq) {
            min_seq = pp.sequence_number;
        }
        
        auto& entry = request_log_[pp.sequence_number];
        entry.pre_prepare = pp;
    }
    
    sequence_number_ = min_seq;
    view_change_messages_.clear();
}

inline void PBFTConsensus::initiate_view_change() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (current_state_ == State::ViewChanging) {
        return;
    }
    
    current_state_ = State::ViewChanging;
    
    ViewChange vc;
    vc.view_number = current_view_ + 1;
    vc.replica_id = replica_id_;
    
    if (state_machine_) {
        vc.checkpoint_sequence = state_machine_->applied_index();
        auto snapshot = state_machine_->get_snapshot();
        vc.checkpoints.resize(1);
        vc.checkpoints[0].sequence_number = vc.checkpoint_sequence;
        vc.checkpoints[0].state_digest = snapshot;
        vc.checkpoints[0].replica_id = replica_id_;
    }
    
    for (const auto& entry_pair : request_log_) {
        if (entry_pair.second.committed) {
            vc.prepared.push_back(entry_pair.second.pre_prepare);
        }
    }
    
    if (message_sender_) {
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(MessageType::ViewChange));
        
        auto view_bytes = reinterpret_cast<const uint8_t*>(&vc.view_number);
        data.insert(data.end(), view_bytes, view_bytes + sizeof(uint64_t));
        
        auto seq_bytes = reinterpret_cast<const uint8_t*>(&vc.checkpoint_sequence);
        data.insert(data.end(), seq_bytes, seq_bytes + sizeof(uint64_t));
        
        auto rep_bytes = reinterpret_cast<const uint8_t*>(&vc.replica_id);
        data.insert(data.end(), rep_bytes, rep_bytes + sizeof(size_t));
        
        broadcast_message(MessageType::ViewChange, data);
    }
    
    lock.unlock();
    
    auto timeout = config_.view_change_timeout;
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        {
            std::lock_guard<std::mutex> lock2(mutex_);
            if (current_state_ == State::Normal) {
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

inline std::vector<PrePrepare> PBFTConsensus::get_prepared_requests() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    std::vector<PrePrepare> result;
    
    for (const auto& entry_pair : request_log_) {
        if (has_quorum_prepare(entry_pair.second)) {
            result.push_back(entry_pair.second.pre_prepare);
        }
    }
    
    return result;
}

inline Checkpoint PBFTConsensus::get_stable_checkpoint() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    Checkpoint cp;
    cp.replica_id = replica_id_;
    
    if (state_machine_) {
        cp.sequence_number = state_machine_->applied_index();
        cp.state_digest = state_machine_->get_snapshot();
    }
    
    return cp;
}

#include "replicated_state.hpp"

}

#endif
