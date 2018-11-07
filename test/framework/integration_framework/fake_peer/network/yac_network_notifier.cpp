/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "framework/integration_framework/fake_peer/network/yac_network_notifier.hpp"

#include "consensus/yac/messages.hpp"
#include "consensus/yac/transport/impl/network_impl.hpp"
#include "consensus/yac/transport/yac_network_interface.hpp"

namespace integration_framework {
  namespace fake_peer {

    void YacNetworkNotifier::onState(YacNetworkNotifier::StateMessage state) {
      auto state_ptr = std::make_shared<const StateMessage>(std::move(state));
      votes_subject_.get_subscriber().on_next(state_ptr);
    }

    rxcpp::observable<YacMessagePtr> YacNetworkNotifier::get_observable() {
      return votes_subject_.get_observable();
    }

  }  // namespace fake_peer
}  // namespace integration_framework
