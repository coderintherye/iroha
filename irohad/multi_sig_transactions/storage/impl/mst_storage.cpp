/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <utility>

#include "multi_sig_transactions/storage/mst_storage.hpp"

namespace iroha {
  MstStorage::MstStorage() {
    log_ = logger::log("MstStorage");
  }

  StateUpdateResult MstStorage::apply(
      const shared_model::crypto::PublicKey &target_peer_key,
      const MstState &new_state) {
    std::lock_guard<std::mutex> lock{this->mutex_};
    return applyImpl(target_peer_key, new_state);
  }

  StateUpdateResult MstStorage::updateOwnState(const DataType &tx) {
    std::lock_guard<std::mutex> lock{this->mutex_};
    return updateOwnStateImpl(tx);
  }

  MstState MstStorage::getExpiredTransactions(const TimeType &current_time) {
    std::lock_guard<std::mutex> lock{this->mutex_};
    return getExpiredTransactionsImpl(current_time);
  }

  MstState MstStorage::getDiffState(
      const shared_model::crypto::PublicKey &target_peer_key,
      const TimeType &current_time) {
    std::lock_guard<std::mutex> lock{this->mutex_};
    return getDiffStateImpl(target_peer_key, current_time);
  }

  MstState MstStorage::whatsNew(ConstRefState new_state) const {
    std::lock_guard<std::mutex> lock{this->mutex_};
    return whatsNewImpl(new_state);
  }
}  // namespace iroha
