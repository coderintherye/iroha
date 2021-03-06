/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <grpc++/grpc++.h>
#include <gtest/gtest.h>

#include "framework/test_subscriber.hpp"

#include "builders/protobuf/transaction.hpp"
#include "module/irohad/network/network_mocks.hpp"
#include "module/shared_model/builders/protobuf/proposal.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_proposal_builder.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "ordering/impl/ordering_gate_impl.hpp"
#include "ordering/impl/ordering_gate_transport_grpc.hpp"

using namespace iroha;
using namespace iroha::ordering;
using namespace iroha::network;
using namespace framework::test_subscriber;
using namespace std::chrono_literals;
using namespace iroha::synchronizer;

using ::testing::_;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;

using shared_model::interface::types::HeightType;

class MockOrderingGateTransportGrpcService
    : public proto::OrderingServiceTransportGrpc::Service {};

class MockOrderingGateTransport : public OrderingGateTransport {
  MOCK_METHOD1(subscribe, void(std::shared_ptr<OrderingGateNotification>));
  MOCK_METHOD1(
      propagateBatch,
      void(std::shared_ptr<shared_model::interface::TransactionBatch>));
};

class OrderingGateTest : public ::testing::Test {
 public:
  OrderingGateTest()
      : fake_service{std::make_shared<MockOrderingGateTransportGrpcService>()} {
  }

  void SetUp() override {
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort(
        "0.0.0.0:0", grpc::InsecureServerCredentials(), &port);

    builder.RegisterService(fake_service.get());

    server = builder.BuildAndStart();
    auto address = "0.0.0.0:" + std::to_string(port);
    // Initialize components after port has been bind
    async_call_ =
        std::make_shared<network::AsyncGrpcClient<google::protobuf::Empty>>();
    transport =
        std::make_shared<OrderingGateTransportGrpc>(address, async_call_);
    gate_impl = std::make_shared<OrderingGateImpl>(transport, 1, false);
    transport->subscribe(gate_impl);

    ASSERT_NE(port, 0);
    ASSERT_TRUE(server);
  }

  void TearDown() override {
    server->Shutdown();
  }

  std::unique_ptr<grpc::Server> server;

  std::shared_ptr<OrderingGateTransportGrpc> transport;
  std::shared_ptr<OrderingGateImpl> gate_impl;
  std::shared_ptr<MockOrderingGateTransportGrpcService> fake_service;
  std::condition_variable cv;
  std::mutex m;
  std::shared_ptr<network::AsyncGrpcClient<google::protobuf::Empty>>
      async_call_;
};

/**
 * @given Initialized OrderingGate
 * @when  Emulation of receiving proposal from the network
 * @then  Round starts <==> proposal is emitted to subscribers
 */
TEST_F(OrderingGateTest, ProposalReceivedByGateWhenSent) {
  auto wrapper = make_test_subscriber<CallExact>(gate_impl->onProposal(), 1);
  wrapper.subscribe();

  auto pcs = std::make_shared<MockPeerCommunicationService>();
  rxcpp::subjects::subject<SynchronizationEvent> commit_subject;
  EXPECT_CALL(*pcs, on_commit())
      .WillOnce(Return(commit_subject.get_observable()));
  gate_impl->setPcs(*pcs);

  grpc::ServerContext context;

  std::vector<shared_model::proto::Transaction> txs;
  txs.push_back(shared_model::proto::TransactionBuilder()
                    .createdTime(iroha::time::now())
                    .creatorAccountId("admin@ru")
                    .addAssetQuantity("coin#coin", "1.0")
                    .quorum(1)
                    .build()
                    .signAndAddSignature(
                        shared_model::crypto::DefaultCryptoAlgorithmType::
                            generateKeypair())
                    .finish());
  iroha::protocol::Proposal proposal = shared_model::proto::ProposalBuilder()
                                           .height(2)
                                           .createdTime(iroha::time::now())
                                           .transactions(txs)
                                           .build()
                                           .getTransport();

  google::protobuf::Empty response;

  transport->onProposal(&context, &proposal, &response);

  ASSERT_TRUE(wrapper.validate());
}

class QueueBehaviorTest : public ::testing::Test {
 public:
  QueueBehaviorTest() : ordering_gate(transport, 1, false){};

  void SetUp() override {
    transport = std::make_shared<MockOrderingGateTransport>();
    pcs = std::make_shared<MockPeerCommunicationService>();
    EXPECT_CALL(*pcs, on_commit())
        .WillOnce(Return(commit_subject.get_observable()));

    ordering_gate.setPcs(*pcs);
    ordering_gate.onProposal().subscribe(
        [&](auto val) { messages.push_back(val); });
  }

  std::shared_ptr<MockOrderingGateTransport> transport;
  std::shared_ptr<MockPeerCommunicationService> pcs;
  rxcpp::subjects::subject<SynchronizationEvent> commit_subject;
  OrderingGateImpl ordering_gate;
  std::vector<decltype(ordering_gate.onProposal())::value_type> messages;

  void pushCommit(HeightType height) {
    commit_subject.get_subscriber().on_next(SynchronizationEvent{
        rxcpp::observable<>::just(
            std::static_pointer_cast<shared_model::interface::Block>(
                std::make_shared<shared_model::proto::Block>(
                    TestBlockBuilder().height(height).build()))),
        SynchronizationOutcomeType::kCommit,
        {height, 1}});
  }

  void pushProposal(HeightType height) {
    ordering_gate.onProposal(std::make_shared<shared_model::proto::Proposal>(
        TestProposalBuilder().height(height).build()));
  };
};

/**
 * @given Initialized OrderingGate
 *        AND MockPeerCommunicationService
 * @when  Send two proposals
 *        AND one commit in node
 * @then  Check that send round appears after commit
 */
TEST_F(QueueBehaviorTest, SendManyProposals) {
  auto wrapper_before =
      make_test_subscriber<CallExact>(ordering_gate.onProposal(), 1);
  wrapper_before.subscribe();
  auto wrapper_after =
      make_test_subscriber<CallExact>(ordering_gate.onProposal(), 2);
  wrapper_after.subscribe();

  std::vector<shared_model::proto::Transaction> txs;
  txs.push_back(shared_model::proto::TransactionBuilder()
                    .createdTime(iroha::time::now())
                    .creatorAccountId("admin@ru")
                    .addAssetQuantity("coin#coin", "1.0")
                    .quorum(1)
                    .build()
                    .signAndAddSignature(
                        shared_model::crypto::DefaultCryptoAlgorithmType::
                            generateKeypair())
                    .finish());
  auto proposal1 = std::make_shared<shared_model::proto::Proposal>(
      shared_model::proto::ProposalBuilder()
          .height(2)
          .createdTime(iroha::time::now())
          .transactions(txs)
          .build());
  auto proposal2 = std::make_shared<shared_model::proto::Proposal>(
      shared_model::proto::ProposalBuilder()
          .height(3)
          .createdTime(iroha::time::now())
          .transactions(txs)
          .build());

  ordering_gate.onProposal(proposal1);
  ordering_gate.onProposal(proposal2);

  ASSERT_TRUE(wrapper_before.validate());

  std::shared_ptr<shared_model::interface::Block> block =
      std::make_shared<shared_model::proto::Block>(
          TestBlockBuilder().height(2).build());

  commit_subject.get_subscriber().on_next(
      SynchronizationEvent{rxcpp::observable<>::just(block),
                           SynchronizationOutcomeType::kCommit,
                           {block->height(), 1}});

  ASSERT_TRUE(wrapper_after.validate());
}

/**
 * @given Initialized OrderingGate
 * AND MockPeerCommunicationService
 * @when Receive proposals in random order
 * @then onProposal output is ordered
 */
TEST_F(QueueBehaviorTest, ReceiveUnordered) {
  // this will set unlock_next_ to false, so proposals 3 and 4 are enqueued
  pushProposal(2);

  pushProposal(4);
  pushProposal(3);

  pushCommit(2);
  pushCommit(3);

  ASSERT_EQ(3, messages.size());
  ASSERT_EQ(2, getProposalUnsafe(messages.at(0))->height());
  ASSERT_EQ(3, getProposalUnsafe(messages.at(1))->height());
  ASSERT_EQ(4, getProposalUnsafe(messages.at(2))->height());
}

/**
 * @given Initialized OrderingGate
 * AND MockPeerCommunicationService
 * @when Receive commits which are newer than existing proposals
 * @then onProposal is not invoked on proposals
 * which are older than last committed block
 */
TEST_F(QueueBehaviorTest, DiscardOldProposals) {
  pushProposal(2);
  pushProposal(3);

  pushProposal(4);
  pushProposal(5);
  pushCommit(4);

  // proposals 2 and 3 must not be forwarded down the pipeline.
  EXPECT_EQ(2, messages.size());
  ASSERT_EQ(2, getProposalUnsafe(messages.at(0))->height());
  ASSERT_EQ(5, getProposalUnsafe(messages.at(1))->height());
}

/**
 * @given Initialized OrderingGate
 * AND MockPeerCommunicationService
 * @when Proposals are newer than received commits
 * @then newer proposals are kept in queue
 */
TEST_F(QueueBehaviorTest, KeepNewerProposals) {
  pushProposal(2);
  pushProposal(3);
  pushProposal(4);

  pushCommit(2);

  // proposal 3 must  be forwarded down the pipeline, 4 kept in queue.
  EXPECT_EQ(2, messages.size());
  EXPECT_EQ(2, getProposalUnsafe(messages.at(0))->height());
  EXPECT_EQ(3, getProposalUnsafe(messages.at(1))->height());

  pushCommit(3);
  // Now proposal 4 is forwarded to the pipeline
  EXPECT_EQ(3, messages.size());
  EXPECT_EQ(4, getProposalUnsafe(messages.at(2))->height());
}

/**
 * @given Initialized OrderingGate
 * AND MockPeerCommunicationService
 * @when commit is received before any proposals
 * @then old proposals are discarded and new is propagated
 */
TEST_F(QueueBehaviorTest, CommitBeforeProposal) {
  pushCommit(4);

  // Old proposals should be discarded
  pushProposal(2);
  pushProposal(3);
  pushProposal(4);

  EXPECT_EQ(0, messages.size());

  // should be propagated
  pushProposal(5);

  // should not be propagated
  pushProposal(6);

  EXPECT_EQ(1, messages.size());
  EXPECT_EQ(5, getProposalUnsafe(messages.at(0))->height());
}

/**
 * @given Initialized OrderingGate
 * AND MockPeerCommunicationService
 * @when commit is received which newer than all proposals
 * @then all proposals are discarded and none are propagated
 */
TEST_F(QueueBehaviorTest, CommitNewerThanAllProposals) {
  pushProposal(2);
  // Old proposals should be discarded
  pushProposal(3);
  pushProposal(4);

  pushCommit(4);
  EXPECT_EQ(1, messages.size());
  EXPECT_EQ(2, getProposalUnsafe(messages.at(0))->height());
}
