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
#ifndef NVIDIA_GXF_CORE_ENTITY_POOL_HOOKS_HPP_
#define NVIDIA_GXF_CORE_ENTITY_POOL_HOOKS_HPP_

#include "gxf/core/gxf.h"

namespace nvidia {
namespace gxf {

// Hook points that let the EntityPool living in gxf/std (libgxf_std.so) plug
// into the GXF core runtime (libgxf_core.so) without creating a core -> std
// link dependency. All hooks default to nullptr (pooling disabled) and are
// installed by gxf/std/entity_pool.cpp when libgxf_std.so is loaded.
//
// - try_return: called by Runtime::GxfEntityDestroyImpl before real
//   destruction; return true if the entity was recycled into a pool.
// - try_acquire: called by Runtime::GxfCreateEntity for unnamed, non-program
//   entities; return true (and set eid/item_ptr) if a pooled entity was
//   recycled.
// - release_context: called during context destruction so pool globals never
//   outlive their GXF context.
using EntityPoolTryReturnHook = bool (*)(gxf_context_t context, gxf_uid_t eid);
using EntityPoolTryAcquireHook = bool (*)(gxf_context_t context, gxf_uid_t& eid,
                                          void** item_ptr);
using EntityPoolReleaseContextHook = void (*)(gxf_context_t context);

extern EntityPoolTryReturnHook g_entity_pool_try_return_hook;
extern EntityPoolTryAcquireHook g_entity_pool_try_acquire_hook;
extern EntityPoolReleaseContextHook g_entity_pool_release_context_hook;

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_CORE_ENTITY_POOL_HOOKS_HPP_
