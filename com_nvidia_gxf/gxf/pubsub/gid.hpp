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
#ifndef NVIDIA_GXF_PUBSUB_GID_HPP_
#define NVIDIA_GXF_PUBSUB_GID_HPP_

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "gxf/core/expected.hpp"

namespace nvidia {
namespace gxf {

/// Size of the GID in bytes (128 bits)
constexpr size_t kGidSize = 16;

/// @brief Global Identifier (GID) for pub/sub endpoints
///
/// An opaque 128-bit random identifier used to identify publishers and
/// subscribers in the pub/sub system. GIDs can be serialized to/from
/// hexadecimal strings for discovery protocols.
///
/// Thread Safety: All const methods are thread-safe. Generation is thread-safe.
struct Gid {
  /// The 128-bit identifier data
  std::array<uint8_t, kGidSize> data;

  /// Default constructor creates a null GID (all zeros)
  Gid();

  /// Construct from raw data
  explicit Gid(const std::array<uint8_t, kGidSize>& bytes);

  /// Equality comparison
  bool operator==(const Gid& other) const;

  /// Inequality comparison
  bool operator!=(const Gid& other) const;

  /// Less-than comparison for use in ordered containers (std::set, std::map)
  bool operator<(const Gid& other) const;

  /// @brief Convert to hexadecimal string representation
  /// @return 32-character hexadecimal string (lowercase)
  std::string to_string() const;

  /// @brief Parse a GID from its hexadecimal string representation
  /// @param str 32-character hexadecimal string
  /// @return The parsed GID, or an error if parsing fails
  static Expected<Gid> from_string(const std::string& str);

  /// @brief Generate a new unique GID (128-bit random)
  /// @return A newly generated unique GID
  static Gid generate();

  /// @brief Create a null GID (all zeros)
  /// @return A null GID
  static Gid null();

  /// @brief Check if this GID is null (all zeros)
  /// @return true if this GID is null
  bool is_null() const;
};

/// Type alias for publisher GIDs (semantic clarity)
using PublisherGid = Gid;

/// Type alias for subscriber GIDs (semantic clarity)
using SubscriberGid = Gid;

}  // namespace gxf
}  // namespace nvidia

/// Hash specialization for std::unordered_map/std::unordered_set support
template <>
struct std::hash<nvidia::gxf::Gid> {
  std::size_t operator()(const nvidia::gxf::Gid& gid) const noexcept;
};

#endif  // NVIDIA_GXF_PUBSUB_GID_HPP_
