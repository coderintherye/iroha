/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ordering/impl/on_demand_os_client_grpc.hpp"

#include <gtest/gtest.h>
#include "backend/protobuf/transaction.hpp"
#include "framework/mock_stream.h"
#include "interfaces/iroha_internal/proposal.hpp"
#include "interfaces/iroha_internal/transaction_batch_impl.hpp"
#include "ordering_mock.grpc.pb.h"

using namespace iroha;
using namespace iroha::ordering;
using namespace iroha::ordering::transport;

using grpc::testing::MockClientAsyncResponseReader;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

struct OnDemandOsClientGrpcTest : public ::testing::Test {
  void SetUp() override {
    auto ustub = std::make_unique<proto::MockOnDemandOrderingStub>();
    stub = ustub.get();
    async_call =
        std::make_shared<network::AsyncGrpcClient<google::protobuf::Empty>>(
            logger::log("AsyncGrpcClient"));
    client = std::make_shared<OnDemandOsClientGrpc>(
        std::move(ustub),
        async_call,
        [&] { return timepoint; },
        timeout,
        logger::log("OnDemandOsClientGrpc"));
  }

  proto::MockOnDemandOrderingStub *stub;
  std::shared_ptr<network::AsyncGrpcClient<google::protobuf::Empty>> async_call;
  OnDemandOsClientGrpc::TimepointType timepoint;
  std::chrono::milliseconds timeout{1};
  std::shared_ptr<OnDemandOsClientGrpc> client;
  consensus::Round round{1, 2};
};

/**
 * @given client
 * @when onBatches is called
 * @then data is correctly serialized and sent
 */
TEST_F(OnDemandOsClientGrpcTest, onBatches) {
  proto::BatchesRequest request;
  auto r = std::make_unique<
      MockClientAsyncResponseReader<google::protobuf::Empty>>();
  EXPECT_CALL(*stub, AsyncSendBatchesRaw(_, _, _))
      .WillOnce(DoAll(SaveArg<1>(&request), Return(r.get())));

  OdOsNotification::CollectionType collection;
  auto creator = "test";
  protocol::Transaction tx;
  tx.mutable_payload()->mutable_reduced_payload()->set_creator_account_id(
      creator);
  collection.push_back(
      std::make_unique<shared_model::interface::TransactionBatchImpl>(
          shared_model::interface::types::SharedTxsCollectionType{
              std::make_unique<shared_model::proto::Transaction>(tx)}));
  client->onBatches(round, std::move(collection));

  ASSERT_EQ(request.round().block_round(), round.block_round);
  ASSERT_EQ(request.round().reject_round(), round.reject_round);
  ASSERT_EQ(request.transactions()
                .Get(0)
                .payload()
                .reduced_payload()
                .creator_account_id(),
            creator);
}

/**
 * Separate action required because ClientContext is non-copyable
 */
ACTION_P(SaveClientContextDeadline, deadline) {
  *deadline = arg0->deadline();
}

/**
 * @given client
 * @when onRequestProposal is called
 * AND proposal returned
 * @then data is correctly serialized and sent
 * AND reply is correctly deserialized
 */
TEST_F(OnDemandOsClientGrpcTest, onRequestProposal) {
  std::chrono::system_clock::time_point deadline;
  proto::ProposalRequest request;
  auto creator = "test";
  proto::ProposalResponse response;
  response.mutable_proposal()
      ->add_transactions()
      ->mutable_payload()
      ->mutable_reduced_payload()
      ->set_creator_account_id(creator);
  EXPECT_CALL(*stub, RequestProposal(_, _, _))
      .WillOnce(DoAll(SaveClientContextDeadline(&deadline),
                      SaveArg<1>(&request),
                      SetArgPointee<2>(response),
                      Return(grpc::Status::OK)));

  auto proposal = client->onRequestProposal(round);

  ASSERT_EQ(timepoint + timeout, deadline);
  ASSERT_EQ(request.round().block_round(), round.block_round);
  ASSERT_EQ(request.round().reject_round(), round.reject_round);
  ASSERT_TRUE(proposal);
  ASSERT_EQ(proposal.value()->transactions()[0].creatorAccountId(), creator);
}

/**
 * @given client
 * @when onRequestProposal is called
 * AND no proposal returned
 * @then data is correctly serialized and sent
 * AND reply is correctly deserialized
 */
TEST_F(OnDemandOsClientGrpcTest, onRequestProposalNone) {
  std::chrono::system_clock::time_point deadline;
  proto::ProposalRequest request;
  proto::ProposalResponse response;
  EXPECT_CALL(*stub, RequestProposal(_, _, _))
      .WillOnce(DoAll(SaveClientContextDeadline(&deadline),
                      SaveArg<1>(&request),
                      SetArgPointee<2>(response),
                      Return(grpc::Status::OK)));

  auto proposal = client->onRequestProposal(round);

  ASSERT_EQ(timepoint + timeout, deadline);
  ASSERT_EQ(request.round().block_round(), round.block_round);
  ASSERT_EQ(request.round().reject_round(), round.reject_round);
  ASSERT_FALSE(proposal);
}
