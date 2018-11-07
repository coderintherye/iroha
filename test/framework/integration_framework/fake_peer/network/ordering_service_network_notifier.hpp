/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FAKE_PEER_OS_NETWORK_NOTIFIER_HPP_
#define FAKE_PEER_OS_NETWORK_NOTIFIER_HPP_

#include <rxcpp/rx.hpp>

#include "consensus/yac/transport/yac_network_interface.hpp"
#include "framework/integration_framework/fake_peer/types.hpp"
#include "network/ordering_service_transport.hpp"

namespace integration_framework {
  namespace fake_peer {

    class OsNetworkNotifier final
        : public iroha::network::OrderingServiceNotification {
     public:
      void onBatch(std::unique_ptr<shared_model::interface::TransactionBatch>
                       batch) override;

      rxcpp::observable<OsBatchPtr> get_observable();

     private:
      rxcpp::subjects::subject<OsBatchPtr> batches_subject_;
    };

  }  // namespace fake_peer
}  // namespace integration_framework

#endif /* FAKE_PEER_OS_NETWORK_NOTIFIER_HPP_ */
