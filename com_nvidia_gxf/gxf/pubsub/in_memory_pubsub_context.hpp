/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef NVIDIA_GXF_PUBSUB_IN_MEMORY_PUBSUB_CONTEXT_HPP_
#define NVIDIA_GXF_PUBSUB_IN_MEMORY_PUBSUB_CONTEXT_HPP_

#include <memory>
#include <mutex>
#include <vector>

#include "gxf/core/parameter.hpp"
#include "gxf/pubsub/in_memory_discovery.hpp"
#include "gxf/pubsub/in_memory_serializer.hpp"
#include "gxf/pubsub/in_memory_transport.hpp"
#include "gxf/pubsub/pubsub_context.hpp"

namespace nvidia {
namespace gxf {

/// @brief Concrete in-memory backends grouped for typed access
///
/// Owned by `InMemoryPubSubContext` and accessible via the typed
/// `in_memory_*()` getters after `initialize()`, e.g. to inject fault patterns
/// or inspect state in tests.
struct InMemoryBackends {
  std::shared_ptr<InMemoryDiscovery>   discovery;
  std::shared_ptr<InMemoryTransport>   transport;
  std::shared_ptr<InMemorySerializer>  serializer;
};

/// @brief `PubSubContext` subclass that self-configures with in-memory backends
///
/// Inherits from `PubSubContext` (a `NetworkContext`) so it can be used
/// directly as the `network_context` referenced by `PubSubTransmitter` and
/// `PubSubReceiver`, with no companion component required.
///
/// Usage in YAML:
/// @code
/// name: pubsub_setup
/// components:
/// - name: network_context
///   type: nvidia::gxf::InMemoryPubSubContext
///   parameters:
///     node_name:        "test_node"
///     serializer_mode:  0    # 0=kPassthrough (default), 1=kFullSerialization
///     drop_pattern:     []   # optional fault injection
///     reorder_pattern:  []
/// @endcode
///
/// All pub/sub parameters (`node_name`, `serializer_mode`, `drop_pattern`,
/// `reorder_pattern`) live in one entity. `init_context()` is called by the
/// scheduler at graph activation — no companion component is required.
///
/// The typed `in_memory_*()` getters provide access to the concrete backend
/// types after `initialize()` for fault injection or assertion in tests.
///
/// Thread Safety: `PubSubContext` guarantees that all public methods are
/// thread-safe, and this subclass preserves that contract. Normal GXF
/// lifecycle callbacks are typically invoked sequentially, but explicit calls
/// before graph activation (for example backend setup from Holoscan) can race
/// with scheduler-driven initialization in edge cases. `initialize()` is
/// protected by `init_mutex_` to keep that path thread-safe, and the typed
/// getters are safe to call from any thread after `initialize()`.
class InMemoryPubSubContext : public PubSubContext {
 public:
  gxf_result_t registerInterface(Registrar* registrar) override;
  gxf_result_t initialize() override;
  gxf_result_t deinitialize() override;

  //----------------------------------------------------------------------------
  // Typed backend accessors
  // PubSubContext also exposes discovery()/transport()/serializer() returning
  // the abstract base types; these return the concrete in-memory types.
  //----------------------------------------------------------------------------

  std::shared_ptr<InMemoryDiscovery>  in_memory_discovery()  const { return backends_.discovery;  }
  std::shared_ptr<InMemoryTransport>  in_memory_transport()  const { return backends_.transport;  }
  std::shared_ptr<InMemorySerializer> in_memory_serializer() const { return backends_.serializer; }

 private:
  /// 0 = kPassthrough (default), 1 = kFullSerialization
  Parameter<int32_t>               serializer_mode_;
  Parameter<std::vector<int32_t>>  drop_pattern_;
  Parameter<std::vector<int32_t>>  reorder_pattern_;

  std::mutex init_mutex_;       ///< Keeps initialize() thread-safe for pre-activation and scheduler overlap
  InMemoryBackends backends_;
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_IN_MEMORY_PUBSUB_CONTEXT_HPP_
