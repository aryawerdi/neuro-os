#include <gtest/gtest.h>
#include "neuro_os/distributed/pbft_consensus.hpp"
#include <memory>

using namespace neuro_os::distributed;

class MockCryptoSigner : public CryptoSigner {
public:
    std::vector<uint8_t> sign(const std::vector<uint8_t>& data) override {
        return data;
    }
    
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, ReplicaId sender) override {
        return data == signature;
    }
};

TEST(PBFTTest, Initialization) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    config.view_change_timeout = std::chrono::milliseconds(3000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    EXPECT_EQ(pbft.get_replica_id(), 0);
    EXPECT_EQ(pbft.get_current_view(), 0);
}

TEST(PBFTTest, PrimaryReplica) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    EXPECT_TRUE(pbft.is_primary());
    EXPECT_EQ(pbft.get_primary_id(), 0);
}

TEST(PBFTTest, ReplicaNotPrimary) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    pbft.set_reachable(1, true);
    pbft.set_reachable(2, true);
    pbft.set_reachable(3, true);
}

TEST(PBFTTest, MessageTypes) {
    PBFTMessage msg;
    msg.type = PBFTMessageType::PREPREPARE;
    msg.view = 1;
    msg.sequence = 100;
    msg.sender = 0;
    
    EXPECT_EQ(msg.type, PBFTMessageType::PREPREPARE);
    EXPECT_EQ(msg.view, 1);
    EXPECT_EQ(msg.sequence, 100);
    EXPECT_EQ(msg.sender, 0);
}

TEST(PBFTTest, ViewChange) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    pbft.handle_view_change(1);
    
    EXPECT_EQ(pbft.get_current_view(), 1);
}

TEST(PBFTTest, ReplicaReachability) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    pbft.set_reachable(1, false);
    pbft.set_reachable(2, false);
    
    EXPECT_FALSE(pbft.is_reachable(1));
    EXPECT_FALSE(pbft.is_reachable(2));
    EXPECT_TRUE(pbft.is_reachable(0));
}

TEST(PBFTConsensusTest, ProposeValue) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    std::vector<uint8_t> value{1, 2, 3, 4};
    auto result = pbft.propose(value);
    
    EXPECT_FALSE(result.success);
}

TEST(ReplicatedStateMachineTest, BasicOperations) {
    ReplicatedStateMachine rsm;
    
    PBFTMessage msg;
    msg.data = {1, 2, 3, 4};
    
    rsm.apply(msg);
    
    EXPECT_EQ(rsm.get_operation_count(), 1);
}

TEST(ReplicatedStateMachineTest, MultipleOperations) {
    ReplicatedStateMachine rsm;
    
    for (int i = 0; i < 10; ++i) {
        PBFTMessage msg;
        msg.data = {static_cast<uint8_t>(i)};
        rsm.apply(msg);
    }
    
    EXPECT_EQ(rsm.get_operation_count(), 10);
}

TEST(ReplicatedStateMachineTest, ClearState) {
    ReplicatedStateMachine rsm;
    
    PBFTMessage msg;
    msg.data = {1, 2, 3};
    rsm.apply(msg);
    
    EXPECT_EQ(rsm.get_operation_count(), 1);
    
    rsm.clear();
    
    EXPECT_EQ(rsm.get_operation_count(), 0);
}

TEST(PBFTTest, SequenceNumber) {
    PBFTConfig config;
    config.replica_count = 4;
    config.byzantine_tolerance = 1;
    config.timeout = std::chrono::milliseconds(1000);
    
    PBFTConsensus pbft;
    pbft.initialize(config, std::make_shared<MockCryptoSigner>());
    
    EXPECT_EQ(pbft.get_sequence_number(), 0);
}
