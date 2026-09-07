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
// Gid is a pure value type; it is implemented for real (not stubbed) because
// the stub components use it in containers and log statements.

#include "gxf/pubsub/gid.hpp"

#include <atomic>
#include <cstdio>
#include <random>

namespace nvidia {
namespace gxf {

Gid::Gid() : data{} {}

Gid::Gid(const std::array<uint8_t, kGidSize>& bytes) : data(bytes) {}

bool Gid::operator==(const Gid& other) const { return data == other.data; }

bool Gid::operator!=(const Gid& other) const { return data != other.data; }

bool Gid::operator<(const Gid& other) const { return data < other.data; }

std::string Gid::to_string() const {
  char buf[2 * kGidSize + 1];
  for (size_t i = 0; i < kGidSize; ++i) {
    std::snprintf(buf + 2 * i, 3, "%02x", data[i]);
  }
  buf[2 * kGidSize] = '\0';
  return std::string(buf);
}

Expected<Gid> Gid::from_string(const std::string& str) {
  if (str.size() != 2 * kGidSize) {
    return Unexpected{GXF_ARGUMENT_INVALID};
  }
  std::array<uint8_t, kGidSize> bytes{};
  for (size_t i = 0; i < kGidSize; ++i) {
    unsigned int value = 0;
    if (std::sscanf(str.c_str() + 2 * i, "%2x", &value) != 1) {
      return Unexpected{GXF_ARGUMENT_INVALID};
    }
    bytes[i] = static_cast<uint8_t>(value);
  }
  return Gid(bytes);
}

Gid Gid::generate() {
  // 128-bit random identifier. std::random_device is used as the entropy
  // source; a counter is mixed in to guarantee uniqueness even if the
  // entropy source is weak.
  static std::atomic<uint64_t> counter{0};
  std::random_device rd;
  std::array<uint8_t, kGidSize> bytes{};
  uint64_t words[2];
  words[0] = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
  words[1] = ((static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd())) ^
             counter.fetch_add(1, std::memory_order_relaxed);
  for (size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<uint8_t>((words[0] >> (8 * i)) & 0xff);
    bytes[8 + i] = static_cast<uint8_t>((words[1] >> (8 * i)) & 0xff);
  }
  return Gid(bytes);
}

Gid Gid::null() { return Gid(); }

bool Gid::is_null() const {
  for (const auto byte : data) {
    if (byte != 0) { return false; }
  }
  return true;
}

}  // namespace gxf
}  // namespace nvidia

std::size_t std::hash<nvidia::gxf::Gid>::operator()(const nvidia::gxf::Gid& gid) const noexcept {
  // FNV-1a over the 16 raw bytes.
  std::size_t hash = 1469598103934665603ULL;
  for (const auto byte : gid.data) {
    hash ^= static_cast<std::size_t>(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}
