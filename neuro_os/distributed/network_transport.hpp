#ifndef NEURO_OS_DISTRIBUTED_NETWORK_TRANSPORT_HPP
#define NEURO_OS_DISTRIBUTED_NETWORK_TRANSPORT_HPP

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <cstring>
#include <stdexcept>

#include "replicated_state.hpp"
#include "pbft_consensus.hpp"

namespace neuro_os::distributed {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

struct NetworkMessage {
    size_t source_replica;
    size_t target_replica;
    std::vector<uint8_t> data;
    std::chrono::steady_clock::time_point timestamp;
    
    NetworkMessage() : source_replica(0), target_replica(0), timestamp(std::chrono::steady_clock::now()) {}
};

class NetworkTransport {
public:
    using MessageCallback = std::function<void(size_t, const std::vector<uint8_t>&)>;
    using ConnectionCallback = std::function<void(size_t, ConnectionState)>;
    
private:
    struct Connection {
        size_t replica_id;
        ConnectionState state;
        std::chrono::steady_clock::time_point last_heartbeat;
        uint64_t messages_sent;
        uint64_t messages_received;
        
        Connection(size_t id) 
            : replica_id(id)
            , state(ConnectionState::Disconnected)
            , last_heartbeat(std::chrono::steady_clock::now())
            , messages_sent(0)
            , messages_received(0) {}
    };
    
    size_t local_replica_id_;
    size_t replica_count_;
    
    std::unordered_map<size_t, std::shared_ptr<Connection>> connections_;
    std::unordered_map<size_t, std::vector<uint8_t>> message_buffers_;
    
    std::queue<NetworkMessage> incoming_queue_;
    std::queue<NetworkMessage> outgoing_queue_;
    std::mutex queue_mutex_;
    
    std::atomic<bool> running_;
    std::thread network_thread_;
    std::thread processing_thread_;
    
    MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
    
    std::chrono::milliseconds heartbeat_interval_;
    std::chrono::milliseconds message_timeout_;
    
    static constexpr size_t MAX_MESSAGE_SIZE = 1024 * 1024;
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    
    void network_loop();
    void processing_loop();
    void handle_incoming_message(const NetworkMessage& msg);
    bool send_raw(size_t target, const std::vector<uint8_t>& data);
    void update_connection(size_t replica_id, ConnectionState state);
    
public:
    NetworkTransport(size_t local_replica_id, size_t replica_count);
    ~NetworkTransport();
    
    void set_message_callback(MessageCallback callback);
    void set_connection_callback(ConnectionCallback callback);
    
    bool connect_to_replica(size_t replica_id);
    void disconnect_from_replica(size_t replica_id);
    void disconnect_all();
    
    bool send_message(size_t target, const std::vector<uint8_t>& data);
    bool broadcast(const std::vector<uint8_t>& data);
    
    bool try_receive(size_t* source, std::vector<uint8_t>* data);
    
    void start();
    void stop();
    
    ConnectionState get_connection_state(size_t replica_id) const;
    size_t connected_count() const;
    
    void set_heartbeat_interval(std::chrono::milliseconds interval);
    void set_message_timeout(std::chrono::milliseconds timeout);
};

inline NetworkTransport::NetworkTransport(size_t local_replica_id, size_t replica_count)
    : local_replica_id_(local_replica_id)
    , replica_count_(replica_count)
    , running_(false)
    , heartbeat_interval_(1000)
    , message_timeout_(5000) {
    
    if (local_replica_id >= replica_count) {
        throw std::invalid_argument("Invalid local replica ID");
    }
    
    connections_.reserve(replica_count);
    message_buffers_.reserve(replica_count);
}

inline NetworkTransport::~NetworkTransport() {
    stop();
}

inline void NetworkTransport::set_message_callback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

inline void NetworkTransport::set_connection_callback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

inline bool NetworkTransport::connect_to_replica(size_t replica_id) {
    if (replica_id >= replica_count_ || replica_id == local_replica_id_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    auto it = connections_.find(replica_id);
    if (it != connections_.end()) {
        if (it->second->state == ConnectionState::Connected) {
            return true;
        }
    }
    
    auto conn = std::make_shared<Connection>(replica_id);
    conn->state = ConnectionState::Connecting;
    connections_[replica_id] = conn;
    
    if (connection_callback_) {
        connection_callback_(replica_id, ConnectionState::Connecting);
    }
    
    conn->state = ConnectionState::Connected;
    if (connection_callback_) {
        connection_callback_(replica_id, ConnectionState::Connected);
    }
    
    return true;
}

inline void NetworkTransport::disconnect_from_replica(size_t replica_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    auto it = connections_.find(replica_id);
    if (it != connections_.end()) {
        it->second->state = ConnectionState::Disconnected;
        if (connection_callback_) {
            connection_callback_(replica_id, ConnectionState::Disconnected);
        }
    }
}

inline void NetworkTransport::disconnect_all() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    for (auto& pair : connections_) {
        pair.second->state = ConnectionState::Disconnected;
        if (connection_callback_) {
            connection_callback_(pair.first, ConnectionState::Disconnected);
        }
    }
}

inline bool NetworkTransport::send_message(size_t target, const std::vector<uint8_t>& data) {
    if (target >= replica_count_ || target == local_replica_id_) {
        return false;
    }
    
    if (data.size() > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    auto it = connections_.find(target);
    if (it == connections_.end() || it->second->state != ConnectionState::Connected) {
        return false;
    }
    
    NetworkMessage msg;
    msg.source_replica = local_replica_id_;
    msg.target_replica = target;
    msg.data = data;
    msg.timestamp = std::chrono::steady_clock::now();
    
    if (outgoing_queue_.size() < MAX_QUEUE_SIZE) {
        outgoing_queue_.push(msg);
        ++it->second->messages_sent;
        return true;
    }
    
    return false;
}

inline bool NetworkTransport::broadcast(const std::vector<uint8_t>& data) {
    bool success = true;
    
    for (size_t i = 0; i < replica_count_; ++i) {
        if (i != local_replica_id_) {
            if (!send_message(i, data)) {
                success = false;
            }
        }
    }
    
    return success;
}

inline bool NetworkTransport::try_receive(size_t* source, std::vector<uint8_t>* data) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (incoming_queue_.empty()) {
        return false;
    }
    
    NetworkMessage msg = incoming_queue_.front();
    incoming_queue_.pop();
    
    *source = msg.source_replica;
    *data = std::move(msg.data);
    
    auto conn_it = connections_.find(msg.source_replica);
    if (conn_it != connections_.end()) {
        ++conn_it->second->messages_received;
    }
    
    return true;
}

inline void NetworkTransport::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    for (size_t i = 0; i < replica_count_; ++i) {
        if (i != local_replica_id_) {
            connect_to_replica(i);
        }
    }
    
    network_thread_ = std::thread(&NetworkTransport::network_loop, this);
    processing_thread_ = std::thread(&NetworkTransport::processing_loop, this);
}

inline void NetworkTransport::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    disconnect_all();
}

inline void NetworkTransport::network_loop() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            while (!outgoing_queue_.empty()) {
                NetworkMessage msg = outgoing_queue_.front();
                outgoing_queue_.pop();
                
                send_raw(msg.target_replica, msg.data);
            }
        }
        
        auto now = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            for (auto& pair : connections_) {
                if (pair.second->state == ConnectionState::Connected) {
                    if (now - pair.second->last_heartbeat > heartbeat_interval_) {
                        std::vector<uint8_t> heartbeat;
                        heartbeat.push_back(0xFF);
                        send_raw(pair.first, heartbeat);
                        pair.second->last_heartbeat = now;
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

inline void NetworkTransport::processing_loop() {
    while (running_) {
        std::vector<size_t> timeouts;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            auto now = std::chrono::steady_clock::now();
            
            for (auto& pair : connections_) {
                if (pair.second->state == ConnectionState::Connected) {
                    if (now - pair.second->last_heartbeat > message_timeout_) {
                        timeouts.push_back(pair.first);
                    }
                }
            }
        }
        
        for (size_t id : timeouts) {
            update_connection(id, ConnectionState::Failed);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

inline void NetworkTransport::handle_incoming_message(const NetworkMessage& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (incoming_queue_.size() < MAX_QUEUE_SIZE) {
        incoming_queue_.push(msg);
    }
    
    if (message_callback_) {
        message_callback_(msg.source_replica, msg.data);
    }
}

inline bool NetworkTransport::send_raw(size_t target, const std::vector<uint8_t>& data) {
    return true;
}

inline void NetworkTransport::update_connection(size_t replica_id, ConnectionState state) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    auto it = connections_.find(replica_id);
    if (it != connections_.end()) {
        it->second->state = state;
        if (connection_callback_) {
            connection_callback_(replica_id, state);
        }
    }
}

inline ConnectionState NetworkTransport::get_connection_state(size_t replica_id) const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    
    auto it = connections_.find(replica_id);
    if (it != connections_.end()) {
        return it->second->state;
    }
    
    return ConnectionState::Disconnected;
}

inline size_t NetworkTransport::connected_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    
    size_t count = 0;
    for (const auto& pair : connections_) {
        if (pair.second->state == ConnectionState::Connected) {
            ++count;
        }
    }
    
    return count;
}

inline void NetworkTransport::set_heartbeat_interval(std::chrono::milliseconds interval) {
    heartbeat_interval_ = interval;
}

inline void NetworkTransport::set_message_timeout(std::chrono::milliseconds timeout) {
    message_timeout_ = timeout;
}

class PBFTNode {
private:
    PBFTConsensus consensus_;
    ReplicatedStateMachine state_machine_;
    std::unique_ptr<NetworkTransport> transport_;
    
    std::atomic<bool> running_;
    std::thread processing_thread_;
    
    void setup_message_handlers();
    void message_handler(size_t source, const std::vector<uint8_t>& data);
    void processing_loop();
    
public:
    PBFTNode(size_t replica_id, const PBFTConfig& config);
    ~PBFTNode();
    
    void start();
    void stop();
    
    PBFTConsensus& consensus() { return consensus_; }
    ReplicatedStateMachine& state_machine() { return state_machine_; }
    NetworkTransport& transport() { return *transport_; }
    
    template<typename T>
    ConsensusResult propose(const T& value) {
        return consensus_.propose(value);
    }
};

inline PBFTNode::PBFTNode(size_t replica_id, const PBFTConfig& config)
    : running_(false) {
    
    consensus_.initialize(config, replica_id);
    consensus_.set_state_machine(&state_machine_);
    
    transport_ = std::make_unique<NetworkTransport>(replica_id, config.replica_count);
    
    transport_->set_message_callback([this](size_t source, const std::vector<uint8_t>& data) {
        message_handler(source, data);
    });
    
    setup_message_handlers();
}

inline PBFTNode::~PBFTNode() {
    stop();
}

inline void PBFTNode::start() {
    if (running_.exchange(true)) {
        return;
    }
    
    transport_->start();
    processing_thread_ = std::thread(&PBFTNode::processing_loop, this);
}

inline void PBFTNode::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    transport_->stop();
    
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

inline void PBFTNode::setup_message_handlers() {
}

inline void PBFTNode::message_handler(size_t source, const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return;
    }
    
    MessageType type = static_cast<MessageType>(data[0]);
    
    size_t offset = 1;
    
    switch (type) {
        case MessageType::PrePrepare: {
            if (data.size() < offset + sizeof(uint64_t) * 2 + 32) break;
            
            PrePrepare msg;
            std::memcpy(&msg.view_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            std::memcpy(&msg.sequence_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            msg.digest.assign(data.begin() + offset, data.begin() + offset + 32);
            offset += 32;
            
            consensus_.on_pre_prepare(msg);
            break;
        }
        
        case MessageType::Prepare: {
            if (data.size() < offset + sizeof(uint64_t) * 2 + 32 + sizeof(size_t)) break;
            
            Prepare msg;
            std::memcpy(&msg.view_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            std::memcpy(&msg.sequence_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            msg.digest.assign(data.begin() + offset, data.begin() + offset + 32);
            offset += 32;
            
            std::memcpy(&msg.replica_id, data.data() + offset, sizeof(size_t));
            
            consensus_.on_prepare(msg);
            break;
        }
        
        case MessageType::Commit: {
            if (data.size() < offset + sizeof(uint64_t) * 2 + 32 + sizeof(size_t)) break;
            
            Commit msg;
            std::memcpy(&msg.view_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            std::memcpy(&msg.sequence_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            msg.digest.assign(data.begin() + offset, data.begin() + offset + 32);
            offset += 32;
            
            std::memcpy(&msg.replica_id, data.data() + offset, sizeof(size_t));
            
            consensus_.on_commit(msg);
            break;
        }
        
        case MessageType::Checkpoint: {
            if (data.size() < offset + sizeof(uint64_t) + 32 + sizeof(size_t)) break;
            
            Checkpoint msg;
            std::memcpy(&msg.sequence_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            msg.state_digest.assign(data.begin() + offset, data.begin() + offset + 32);
            offset += 32;
            
            std::memcpy(&msg.replica_id, data.data() + offset, sizeof(size_t));
            
            consensus_.on_checkpoint(msg);
            break;
        }
        
        case MessageType::ViewChange: {
            if (data.size() < offset + sizeof(uint64_t) * 2 + sizeof(size_t)) break;
            
            ViewChange msg;
            std::memcpy(&msg.view_number, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            std::memcpy(&msg.checkpoint_sequence, data.data() + offset, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            
            std::memcpy(&msg.replica_id, data.data() + offset, sizeof(size_t));
            
            consensus_.handle_view_change(msg);
            break;
        }
        
        case MessageType::NewView: {
            if (data.size() < offset + sizeof(uint64_t)) break;
            
            NewView msg;
            std::memcpy(&msg.view_number, data.data() + offset, sizeof(uint64_t));
            
            consensus_.on_new_view(msg);
            break;
        }
        
        default:
            break;
    }
}

inline void PBFTNode::processing_loop() {
    while (running_) {
        size_t source;
        std::vector<uint8_t> data;
        
        if (transport_->try_receive(&source, &data)) {
            message_handler(source, data);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

}

#endif
