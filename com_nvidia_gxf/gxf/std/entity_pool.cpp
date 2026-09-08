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

// Functional implementation of the GXF 5.7.1 EntityPool contract
// (gxf/std/entity_pool.hpp) on this GXF 4.1-based tree.
//
// Integration with the core runtime goes through the hook variables defined
// in libgxf_core.so (gxf/core/entity_pool_hooks.hpp), which this file installs
// at library load time. This avoids a core -> std link dependency and avoids
// duplicating the pool registry into every extension .so (this object is
// linked ONLY into libgxf_std.so).

#include "gxf/std/entity_pool.hpp"

#include <algorithm>
#include <cstdlib>

#include "common/logger.hpp"
#include "gxf/core/entity_item.hpp"
#include "gxf/core/entity_pool_hooks.hpp"

namespace nvidia {
namespace gxf {

namespace {

constexpr size_t kDefaultPoolSize = 256;

std::mutex g_registry_mutex;
// All pools registered per context (weak refs; pools are owned by their users)
std::unordered_map<gxf_context_t, std::vector<std::weak_ptr<EntityPool>>> g_pool_registry;
// The shared pool per context (strong ref; used by GetShared/TryAcquireFromPool)
std::unordered_map<gxf_context_t, std::shared_ptr<EntityPool>> g_shared_pools;
// Contexts with pooling disabled (GXF_ENTITY_POOL_SIZE=0)
std::unordered_set<gxf_context_t> g_pool_disabled;
// Contexts whose shared pool is currently being created (recursion guard:
// preallocate() itself calls GxfCreateEntity, which re-enters TryAcquireFromPool)
std::unordered_set<gxf_context_t> g_pool_creating;

size_t envPoolSize(bool* ok) {
  const char* env = std::getenv("GXF_ENTITY_POOL_SIZE");
  if (env == nullptr) { *ok = true; return kDefaultPoolSize; }
  char* end = nullptr;
  const long v = std::strtol(env, &end, 10);
  if (end == env || v < 0) { *ok = false; return 0; }
  *ok = true;
  return static_cast<size_t>(v);
}

}  // namespace

// ---------------------------------------------------------------------------
// PooledEntity
// ---------------------------------------------------------------------------

PooledEntity::PooledEntity(Entity entity, std::shared_ptr<EntityPool> pool)
    : entity_(std::move(entity)), pool_(std::move(pool)) {}

PooledEntity::~PooledEntity() {
  if (pool_ && !entity_.is_null()) {
    // Return the entity explicitly. The runtime return hook
    // (EntityPool::TryReturnToPool) fires again when the wrapped handle's
    // refcount reaches zero, but release() is idempotent (free-list guard),
    // so the entity is never returned twice.
    pool_->release(entity_.eid());
  }
}

PooledEntity::PooledEntity(PooledEntity&& other) noexcept
    : entity_(std::move(other.entity_)), pool_(std::move(other.pool_)) {}

PooledEntity& PooledEntity::operator=(PooledEntity&& other) noexcept {
  if (this != &other) {
    if (pool_ && !entity_.is_null()) { pool_->release(entity_.eid()); }
    entity_ = std::move(other.entity_);
    pool_ = std::move(other.pool_);
  }
  return *this;
}

Entity PooledEntity::release() {
  pool_.reset();
  return std::move(entity_);
}

// ---------------------------------------------------------------------------
// EntityPool
// ---------------------------------------------------------------------------

EntityPool::EntityPool(gxf_context_t context) : context_(context) {}

EntityPool::~EntityPool() {
  if (!properly_released_ || context_ == nullptr) {
    // Context already gone (or unknown): skip GxfEntityDestroy to avoid
    // use-after-free (see entity_pool.hpp).
    return;
  }
  // Fallback path: the pool went away without ReleaseContextPools having been
  // called first (context still valid). destroyFreeEntities() is idempotent
  // and a no-op when ReleaseContextPools already cleaned up.
  destroyFreeEntities();
}

Expected<std::shared_ptr<EntityPool>> EntityPool::Create(gxf_context_t context,
                                                         size_t capacity) {
  if (context == nullptr) { return Unexpected{GXF_CONTEXT_INVALID}; }
  auto pool = std::shared_ptr<EntityPool>(new EntityPool(context));
  const gxf_result_t code = pool->preallocate(capacity);
  if (code != GXF_SUCCESS) { return Unexpected{code}; }
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto& vec = g_pool_registry[context];
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [](const std::weak_ptr<EntityPool>& w) { return w.expired(); }),
              vec.end());
    vec.push_back(pool);
  }
  return pool;
}

std::shared_ptr<EntityPool> EntityPool::GetShared(gxf_context_t context, size_t capacity) {
  if (context == nullptr) { return nullptr; }
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    const auto it = g_shared_pools.find(context);
    if (it != g_shared_pools.end()) { return it->second; }
    if (g_pool_creating.count(context) != 0) {
      // Recursive creation (preallocate -> GxfCreateEntity -> TryAcquireFromPool)
      return nullptr;
    }
    g_pool_creating.insert(context);
  }
  auto created = Create(context, capacity);
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_pool_creating.erase(context);
    if (!created) { return nullptr; }
    const auto it = g_shared_pools.find(context);
    if (it != g_shared_pools.end()) { return it->second; }  // lost a race
    g_shared_pools[context] = *created;
    return *created;
  }
}

gxf_result_t EntityPool::preallocate(size_t count) {
  for (size_t i = 0; i < count; i++) {
    const GxfEntityCreateInfo info{nullptr, 0};
    gxf_uid_t eid = kNullUid;
    const gxf_result_t code = GxfCreateEntity(context_, &info, &eid);
    if (code != GXF_SUCCESS) { return code; }
    // Fetch the EntityItem pointer once (feeds the direct clear path). Use
    // the side-effect-free C API; a temporary Entity::Shared handle would
    // toggle the refcount and destroy the entity on release.
    void* item_ptr = nullptr;
    const gxf_result_t ptr_code = GxfEntityGetItemPtr(context_, eid, &item_ptr);
    if (ptr_code != GXF_SUCCESS) { return ptr_code; }
    auto entry = std::make_unique<PoolEntry>();
    entry->eid = eid;
    entry->entity_item_ptr = item_ptr;
    PoolEntry* raw = entry.get();
    {
      std::lock_guard<std::mutex> lock(entries_mutex_);
      all_entries_.push_back(std::move(entry));
      pool_eids_.insert(eid);
      eid_to_entry_[eid] = raw;
    }
    {
      std::lock_guard<std::mutex> lock(free_list_mutex_);
      free_entries_.push_back(raw);
      available_count_++;
    }
  }
  return GXF_SUCCESS;
}

Expected<Entity> EntityPool::acquire() {
  total_acquires_++;
  PoolEntry* entry = nullptr;
  {
    std::lock_guard<std::mutex> lock(free_list_mutex_);
    if (!free_entries_.empty()) {
      entry = free_entries_.back();
      free_entries_.pop_back();
      available_count_--;
    }
  }
  if (entry != nullptr) {
    // Defense in depth: only hand out uninitialized entities (see
    // TryAcquireFromPool for the rationale).
    if (entry->entity_item_ptr != nullptr &&
        static_cast<EntityItem*>(entry->entity_item_ptr)->stage.load() ==
            Stage::kUninitialized) {
      pool_hits_++;
      entry->in_free_list = false;
      return Entity::Shared(context_, entry->eid, entry->entity_item_ptr);
    }
    GXF_LOG_WARNING("EntityPool::acquire: not handing out eid=%ld (not "
                    "uninitialized); retiring entry",
                    entry->eid);
    // Fall through to the miss path below.
  }
  // Pool exhausted: create an additional tracked entity (pool grows and
  // never shrinks, per the contract).
  pool_misses_++;
  const GxfEntityCreateInfo info{nullptr, 0};
  gxf_uid_t eid = kNullUid;
  const gxf_result_t code = GxfCreateEntity(context_, &info, &eid);
  if (code != GXF_SUCCESS) { return Unexpected{code}; }
  auto entity = Entity::Shared(context_, eid);
  if (!entity) { return Unexpected{entity.error()}; }
  auto extra = std::make_unique<PoolEntry>();
  extra->eid = eid;
  extra->entity_item_ptr = entity->entity_item_ptr();
  extra->in_free_list = false;
  {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    PoolEntry* raw = extra.get();
    all_entries_.push_back(std::move(extra));
    pool_eids_.insert(eid);
    eid_to_entry_[eid] = raw;
  }
  return entity;
}

Expected<PooledEntity> EntityPool::acquirePooled(std::shared_ptr<EntityPool> self) {
  auto entity = acquire();
  if (!entity) { return ForwardError(entity); }
  return PooledEntity(std::move(entity.value()), std::move(self));
}

gxf_result_t EntityPool::clearEntity(gxf_uid_t eid, void* item_ptr) {
  // Strict safety rule (this 4.1-based implementation): only entities in the
  // uninitialized stage are recycled. Entities that were activated by a
  // scheduler may still be referenced by scheduler/executor/queue structures
  // (see greedy_scheduler's dynamic activation), so deactivating and pooling
  // them races with re-activation. Those take the normal destroy path
  // instead (release() propagates the error, TryReturnToPool returns false).
  return (item_ptr != nullptr)
             ? GxfEntityClearComponentsDirect(context_, item_ptr)
             : GxfEntityClearComponents(context_, eid);
}

gxf_result_t EntityPool::release(gxf_uid_t eid) {
  PoolEntry* entry = nullptr;
  {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    const auto it = eid_to_entry_.find(eid);
    if (it == eid_to_entry_.end()) { return GXF_ENTITY_NOT_FOUND; }
    entry = it->second;
  }

  {
    std::lock_guard<std::mutex> lock(free_list_mutex_);
    if (entry->in_free_list) {
      return GXF_SUCCESS;  // idempotent: already returned
    }
  }
  const gxf_result_t code = clearEntity(eid, entry->entity_item_ptr);
  if (code != GXF_SUCCESS) {
    // Not recyclable (e.g. still initialized/scheduler-tracked): the caller
    // (TryReturnToPool) returns false and the entity takes the normal
    // destroy path.
    return code;
  }
  {
    std::lock_guard<std::mutex> lock(free_list_mutex_);
    entry->in_free_list = true;
    free_entries_.push_back(entry);
    available_count_++;
  }
  total_releases_++;
  return GXF_SUCCESS;
}

gxf_result_t EntityPool::release(Entity&& entity) {
  const gxf_uid_t eid = entity.eid();
  // The moved-from handle's destructor fires the runtime return hook once
  // more; release() is idempotent (see above).
  return release(eid);
}

bool EntityPool::isPoolEntity(gxf_uid_t eid) const {
  std::lock_guard<std::mutex> lock(entries_mutex_);
  return pool_eids_.count(eid) != 0;
}

void EntityPool::unregisterGlobally() {
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  const auto it = g_pool_registry.find(context_);
  if (it != g_pool_registry.end()) {
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const std::weak_ptr<EntityPool>& w) {
                               return w.expired() || w.lock().get() == this;
                             }),
              vec.end());
  }
  const auto sit = g_shared_pools.find(context_);
  if (sit != g_shared_pools.end() && sit->second.get() == this) {
    g_shared_pools.erase(sit);
  }
}

bool EntityPool::TryReturnToPool(gxf_context_t context, gxf_uid_t eid) {
  std::shared_ptr<EntityPool> owner;
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    const auto it = g_pool_registry.find(context);
    if (it != g_pool_registry.end()) {
      for (auto& weak : it->second) {
        auto p = weak.lock();
        if (p && p->isPoolEntity(eid)) {
          owner = p;
          break;
        }
      }
    }
  }
  if (!owner) { return false; }
  return owner->release(eid) == GXF_SUCCESS;
}

bool EntityPool::TryAcquireFromPool(gxf_context_t context, gxf_uid_t& eid, void** item_ptr) {
  if (context == nullptr) { return false; }
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    if (g_pool_disabled.count(context) != 0) { return false; }
  }
  bool ok = false;
  const size_t size = envPoolSize(&ok);
  if (!ok || size == 0) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_pool_disabled.insert(context);
    return false;
  }
  auto pool = GetShared(context, size);
  if (!pool) { return false; }
  PoolEntry* entry = nullptr;
  {
    std::lock_guard<std::mutex> lock(pool->free_list_mutex_);
    if (!pool->free_entries_.empty()) {
      entry = pool->free_entries_.back();
      pool->free_entries_.pop_back();
      pool->available_count_--;
    }
  }
  if (entry == nullptr) {
    // No recycled entity available; the runtime falls back to fresh creation
    // (this path intentionally does not grow the pool).
    return false;
  }
  // Defense in depth: never hand out an entity that is not in the
  // uninitialized stage (e.g. re-activated by a scheduler while sitting in
  // the free list). It stays out of circulation; the caller creates fresh.
  if (entry->entity_item_ptr != nullptr &&
      static_cast<EntityItem*>(entry->entity_item_ptr)->stage.load() !=
          Stage::kUninitialized) {
    GXF_LOG_WARNING("EntityPool::TryAcquireFromPool: not handing out eid=%ld "
                    "(stage=%d); retiring entry",
                    entry->eid,
                    static_cast<int>(
                        static_cast<EntityItem*>(entry->entity_item_ptr)->stage.load()));
    return false;
  }
  entry->in_free_list = false;
  pool->total_acquires_++;
  pool->pool_hits_++;
  eid = entry->eid;
  if (item_ptr != nullptr) { *item_ptr = entry->entity_item_ptr; }
  return true;
}

void EntityPool::ReleaseContextPools(gxf_context_t context) {
  std::vector<std::shared_ptr<EntityPool>> dying;
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    const auto it = g_pool_registry.find(context);
    if (it != g_pool_registry.end()) {
      for (auto& weak : it->second) {
        auto p = weak.lock();
        if (p) { dying.push_back(p); }
      }
      g_pool_registry.erase(it);
    }
    const auto sit = g_shared_pools.find(context);
    if (sit != g_shared_pools.end()) {
      dying.push_back(sit->second);
      g_shared_pools.erase(sit);
    }
    g_pool_disabled.erase(context);
    g_pool_creating.erase(context);
  }
  // Clean up the free entities eagerly while the context is definitely still
  // valid. Users may keep their own shared_ptrs beyond this point; the pool
  // destructor then becomes a no-op (destroyFreeEntities is idempotent).
  for (auto& p : dying) {
    p->properly_released_ = true;
    p->destroyFreeEntities();
  }
}

void EntityPool::destroyFreeEntities() {
  // The pool must already be unregistered (so GxfEntityDestroy takes the real
  // destroy path and does not re-enter TryReturnToPool).
  for (auto& entry : all_entries_) {
    if (!entry->in_free_list) { continue; }
    void* item_ptr = nullptr;
    if (GxfEntityGetItemPtr(context_, entry->eid, &item_ptr) != GXF_SUCCESS) {
      // Already destroyed (e.g. by an earlier eager cleanup).
      entry->in_free_list = false;
      continue;
    }
    if (GxfEntityDestroy(context_, entry->eid) == GXF_SUCCESS) {
      entry->in_free_list = false;
    }
  }
}

int64_t EntityPool::available() const {
  return available_count_.load();
}

size_t EntityPool::capacity() const {
  std::lock_guard<std::mutex> lock(entries_mutex_);
  return all_entries_.size();
}

size_t EntityPool::in_use() const {
  return capacity() - static_cast<size_t>(std::max<int64_t>(0, available()));
}

EntityPool::Stats EntityPool::stats() const {
  Stats s;
  s.total_acquires = total_acquires_.load();
  s.total_releases = total_releases_.load();
  s.pool_hits = pool_hits_.load();
  s.pool_misses = pool_misses_.load();
  s.current_available = available();
  s.current_in_use = in_use();
  return s;
}

// ---------------------------------------------------------------------------
// Hook installation (runs when libgxf_std.so is loaded)
// ---------------------------------------------------------------------------
namespace {
struct EntityPoolHookInstaller {
  EntityPoolHookInstaller() {
    g_entity_pool_try_return_hook = &EntityPool::TryReturnToPool;
    g_entity_pool_try_acquire_hook = &EntityPool::TryAcquireFromPool;
    g_entity_pool_release_context_hook = &EntityPool::ReleaseContextPools;
  }
};
EntityPoolHookInstaller g_hook_installer;
}  // namespace

}  // namespace gxf
}  // namespace nvidia
