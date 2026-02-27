#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <chrono>
#include <optional>

namespace neuro_os::distributed {

using ViewNumber = uint64_t;
using SequenceNumber = uint64_t;
using ReplicaId = uint32_t;

struct PBFTConfig {
    size_t replica_count;
    size_t byzantine_tolerance;
    std::chrono::milliseconds timeout;
    std::chrono::milliseconds view_change_timeout;
};

enum class PBFTMessageType {
    PREPREPARE,
    PREPARE,
    COMMIT,
    CHECKPOINT,
    VIEW_CHANGE,
    NEW_VIEW
};

struct PBFTMessage {
    PBFTMessageType type;
    ViewNumber view;
    SequenceNumber sequence;
    ReplicaId sender;
    std::vector<uint8_t> data;
    std::vector<uint8_t> signature;
    
    std::string to_string() const;
};

struct ConsensusResult {
    bool success;
    std::vector<uint8_t> value;
    ViewNumber view;
    SequenceNumber sequence;
    std::string error_message;
};

class CryptoSigner {
public:
    virtual ~CryptoSigner() = default;
    virtual std::vector<uint8_t> sign(const std::vector<uint8_t>& data) = 0;
    virtual bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, ReplicaId sender) = 0;
};

class PBFTConsensus {
public:
    PBFTConsensus();
    ~PBFTConsensus();
    
    void initialize(const PBFTConfig& config, std::shared_ptr<CryptoSigner> signer);
    
    template<typename T>
    ConsensusResult propose(const T& value);
    
    void handle_message(const PBFTMessage& message);
    void handle_view_change(ViewNumber new_view);
    
    ViewNumber get_current_view() const { return current_view_; }
    SequenceNumber get_sequence_number() const { return sequence_number_; }
    ReplicaId get_replica_id() const { return replica_id_; }
    
    bool is_primary() const;
    ReplicaId get_primary_id() const;
    
    void set_reachable(ReplicaId id, bool reachable);
    bool is_reachable(ReplicaId id) const;

private:
    void start_view_change(ViewNumber new_view);
    void send_preprepare(const std::vector<uint8_t>& data);
    void send_prepare(SequenceNumber seq);
    void send_commit(SequenceNumber seq);
    void execute_request(const PBFTMessage& message);
    
    bool has_quorum_of_prepares(SequenceNumber seq) const;
    bool has_quorum_of_commits(SequenceNumber seq) const;
    
    PBFTConfig config_;
    ReplicaId replica_id_;
    ViewNumber current_view_;
    SequenceNumber sequence_number_;
    std::shared_ptr<CryptoSigner> signer_;
    
    std::map<SequenceNumber, std::vector<PBFTMessage>> prepares_;
    std::map<SequenceNumber, std::vector<PBFTMessage>> commits_;
    std::map<SequenceNumber, PBFTMessage> preprepare_;
    
    std::map<ReplicaId, bool> reachable_replicas_;
    std::mutex mutex_;
};

class ReplicatedStateMachine {
public:
    ReplicatedStateMachine();
    
    void apply(const PBFTMessage& message);
    std::vector<uint8_t> get_state() const;
    void set_state(const std::vector<uint8_t>& state);
    
    size_t get_operation_count() const { return operation_count_; }
    void clear();

private:
    std::vector<std::vector<uint8_t>> operations_;
    size_t operation_count_;
    std::mutex mutex_;
};

}
