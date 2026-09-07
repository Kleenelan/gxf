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

// GXF 5.7.1 pub/sub compatibility stub (built on the GXF 4.1 tree).
// Value comparisons are implemented for real; to_yaml() emits a hand-built
// YAML string; from_yaml() is a stub (discovery payload exchange is not
// functional in this build) and returns GXF_NOT_IMPLEMENTED.

#include "gxf/pubsub/endpoint_info.hpp"

#include "common/logger.hpp"

namespace nvidia {
namespace gxf {

bool NativeBufferCapability::operator==(const NativeBufferCapability& other) const {
  return native_buffer_protocols == other.native_buffer_protocols &&
         memory_domain == other.memory_domain && gpu_device_uuid == other.gpu_device_uuid &&
         native_buffer_profile == other.native_buffer_profile &&
         descriptor_format_version == other.descriptor_format_version &&
         host_id == other.host_id;
}

bool NativeBufferCapability::operator!=(const NativeBufferCapability& other) const {
  return !(*this == other);
}

bool EndpointInfo::operator==(const EndpointInfo& other) const {
  return gid == other.gid && topic_name == other.topic_name && type_name == other.type_name &&
         type_hash == other.type_hash && transport_address == other.transport_address &&
         creation_timestamp_ns == other.creation_timestamp_ns && node_name == other.node_name &&
         qos == other.qos && native_buffer_capability == other.native_buffer_capability;
}

bool EndpointInfo::operator!=(const EndpointInfo& other) const { return !(*this == other); }

std::string EndpointInfo::to_yaml() const {
  // Hand-built YAML (no yaml-cpp dependency in this TU).
  std::string out;
  out += "gid: " + gid.to_string();
  out += "\ntopic_name: " + topic_name;
  out += "\ntype_name: " + type_name;
  out += "\ntype_hash: " + type_hash;
  out += "\ntransport_address: " + transport_address;
  out += "\ncreation_timestamp_ns: " + std::to_string(creation_timestamp_ns);
  out += "\nnode_name: " + node_name;
  out += "\nqos:\n";
  const std::string qos_yaml = qos.to_yaml();
  for (size_t pos = 0; pos < qos_yaml.size();) {
    const size_t nl = qos_yaml.find('\n', pos);
    out += "  " + qos_yaml.substr(pos, nl == std::string::npos ? nl : nl - pos) + "\n";
    if (nl == std::string::npos) { break; }
    pos = nl + 1;
  }
  out += "native_buffer_capability:\n";
  out += "  memory_domain: " + native_buffer_capability.memory_domain;
  out += "\n  gpu_device_uuid: " + native_buffer_capability.gpu_device_uuid;
  out += "\n  native_buffer_profile: " + native_buffer_capability.native_buffer_profile;
  out += "\n  descriptor_format_version: " +
         std::to_string(native_buffer_capability.descriptor_format_version);
  out += "\n  host_id: " + native_buffer_capability.host_id;
  out += "\n  native_buffer_protocols:";
  for (const auto& protocol : native_buffer_capability.native_buffer_protocols) {
    out += "\n  - " + protocol;
  }
  out += "\n";
  return out;
}

Expected<EndpointInfo> EndpointInfo::from_yaml(const std::string& yaml) {
  (void)yaml;
  GXF_LOG_ERROR(
      "EndpointInfo::from_yaml is a compatibility stub in this build; "
      "pub/sub discovery payload exchange is not functional");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

bool TopicInfo::operator==(const TopicInfo& other) const {
  return topic_name == other.topic_name && type_name == other.type_name &&
         type_hash == other.type_hash && publisher_count == other.publisher_count &&
         subscriber_count == other.subscriber_count;
}

bool TopicInfo::operator!=(const TopicInfo& other) const { return !(*this == other); }

std::string TopicInfo::to_yaml() const {
  std::string out;
  out += "topic_name: " + topic_name;
  out += "\ntype_name: " + type_name;
  out += "\ntype_hash: " + type_hash;
  out += "\npublisher_count: " + std::to_string(publisher_count);
  out += "\nsubscriber_count: " + std::to_string(subscriber_count);
  out += "\n";
  return out;
}

Expected<TopicInfo> TopicInfo::from_yaml(const std::string& yaml) {
  (void)yaml;
  GXF_LOG_ERROR(
      "TopicInfo::from_yaml is a compatibility stub in this build; "
      "pub/sub discovery payload exchange is not functional");
  return Unexpected{GXF_NOT_IMPLEMENTED};
}

}  // namespace gxf
}  // namespace nvidia
