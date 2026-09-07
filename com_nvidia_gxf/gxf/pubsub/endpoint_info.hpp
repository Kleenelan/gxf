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
#ifndef NVIDIA_GXF_PUBSUB_ENDPOINT_INFO_HPP_
#define NVIDIA_GXF_PUBSUB_ENDPOINT_INFO_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "gxf/core/expected.hpp"
#include "gxf/pubsub/gid.hpp"
#include "gxf/pubsub/qos_profile.hpp"

namespace nvidia {
namespace gxf {

/// Native buffer (e.g. CUDA IPC) capability advertisement.
/// Populated at announce time; propagated to all discovery peers.
struct NativeBufferCapability {
  /// Supported protocols, e.g. {"cuda_ipc"}, {"posix_shm"}, or both.
  /// Empty means no native buffer support.
  std::vector<std::string> native_buffer_protocols;

  /// Convenience: true when at least one native buffer protocol is advertised.
  bool supports_native_buffers() const { return !native_buffer_protocols.empty(); }

  /// "host_only" | "same_host_gpu" | "cross_host_gpu"
  std::string memory_domain;

  /// UUID string from cudaDeviceProp::uuid; empty if not GPU-capable.
  std::string gpu_device_uuid;

  /// Opaque versioned profile identifier agreed between publisher and subscriber,
  /// e.g. "cuda_ipc_same_gpu_v1". Enables future profiles without flag proliferation.
  std::string native_buffer_profile;

  /// Wire format version for the descriptor blob (matches descriptor_format_version
  /// in MessageMetadata). Advertised at discovery time so version mismatches are
  /// caught before the first send, not at runtime.
  uint8_t descriptor_format_version{0};

  /// Stable identifier for the host machine (e.g. Linux /etc/machine-id).
  /// Used for future same-host native-protocol negotiation; empty if unavailable.
  std::string host_id;

  bool operator==(const NativeBufferCapability& other) const;
  bool operator!=(const NativeBufferCapability& other) const;
};

/// @brief Information about a pub/sub endpoint (publisher or subscriber)
///
/// Contains all metadata needed to identify and connect to an endpoint,
/// including addressing information for discovery protocols.
///
/// to_yaml() / from_yaml() are intended for transports that must exchange
/// endpoint metadata out‑of‑band (e.g., Zenoh metadata keys, UCX/GRPC
/// rendezvous). DDS does not use these methods because discovery is automatic;
/// it only uses EndpointInfo in memory.”
struct EndpointInfo {
  /// Unique identifier for this endpoint
  Gid gid;

  /// Topic name (e.g., "/camera/image", "/sensor/imu")
  std::string topic_name;

  /// Human-readable type name (e.g., "holoscan::Tensor")
  std::string type_name;

  /// Type hash for compatibility checking (e.g., SHA256 of schema)
  std::string type_hash;

  /// Backend-specific transport address (e.g., "ucx://192.168.1.10:5000")
  std::string transport_address;

  /// Creation timestamp in nanoseconds since epoch
  uint64_t creation_timestamp_ns = 0;

  /// Name of the fragment this endpoint belongs to
  /// Name of the node (Holoscan fragment) this endpoint belongs to
  std::string node_name;

  /// Quality of Service profile for this endpoint
  QoSProfile qos;

  /// Native buffer capability advertisement for this endpoint
  NativeBufferCapability native_buffer_capability;

  /// Default constructor
  EndpointInfo() = default;

  /// Equality comparison
  bool operator==(const EndpointInfo& other) const;

  /// Inequality comparison
  bool operator!=(const EndpointInfo& other) const;

  /// @brief Serialize to YAML string for discovery protocols
  /// @return YAML representation of this endpoint info
  std::string to_yaml() const;

  /// @brief Parse from YAML string
  /// @param yaml YAML string to parse
  /// @return Parsed EndpointInfo, or error if parsing fails
  static Expected<EndpointInfo> from_yaml(const std::string& yaml);
};

/// @brief Information about a publisher endpoint
///
/// Inherits all fields from EndpointInfo. Provides semantic distinction
/// from SubscriberInfo for type safety and clarity.
struct PublisherInfo : public EndpointInfo {
  /// Default constructor
  PublisherInfo() = default;

  /// Construct from base EndpointInfo
  explicit PublisherInfo(const EndpointInfo& info) : EndpointInfo(info) {}

  /// Construct from base EndpointInfo (move)
  explicit PublisherInfo(EndpointInfo&& info) : EndpointInfo(std::move(info)) {}
};

/// @brief Information about a subscriber endpoint
///
/// Inherits all fields from EndpointInfo. Provides semantic distinction
/// from PublisherInfo for type safety and clarity.
struct SubscriberInfo : public EndpointInfo {
  /// Default constructor
  SubscriberInfo() = default;

  /// Construct from base EndpointInfo
  explicit SubscriberInfo(const EndpointInfo& info) : EndpointInfo(info) {}

  /// Construct from base EndpointInfo (move)
  explicit SubscriberInfo(EndpointInfo&& info) : EndpointInfo(std::move(info)) {}
};

/// @brief Aggregated information about a topic
///
/// Provides summary statistics about publishers and subscribers
/// registered on a particular topic.
struct TopicInfo {
  /// Topic name
  std::string topic_name;

  /// Type name of messages on this topic
  std::string type_name;

  /// Type hash for compatibility checking
  std::string type_hash;

  /// Number of publishers on this topic
  size_t publisher_count = 0;

  /// Number of subscribers on this topic
  size_t subscriber_count = 0;

  /// Default constructor
  TopicInfo() = default;

  /// Equality comparison
  bool operator==(const TopicInfo& other) const;

  /// Inequality comparison
  bool operator!=(const TopicInfo& other) const;

  /// @brief Serialize to YAML string
  /// @return YAML representation of this topic info
  std::string to_yaml() const;

  /// @brief Parse from YAML string
  /// @param yaml YAML string to parse
  /// @return Parsed TopicInfo, or error if parsing fails
  static Expected<TopicInfo> from_yaml(const std::string& yaml);
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_PUBSUB_ENDPOINT_INFO_HPP_
