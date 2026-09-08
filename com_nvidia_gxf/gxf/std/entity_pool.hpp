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
#ifndef NVIDIA_GXF_STD_ENTITY_POOL_HPP_
#define NVIDIA_GXF_STD_ENTITY_POOL_HPP_

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gxf/core/entity.hpp"
#include "gxf/core/expected.hpp"
#include "gxf/core/gxf.h"

namespace nvidia {
namespace gxf {

// Forward declaration
class EntityPool;

/**
 * @brief RAII wrapper for pooled entities that auto-releases back to pool.
 *
 * PooledEntity wraps an Entity and holds a reference to its source pool.
 * When the PooledEntity is destroyed (e.g., goes out of scope, or downstream
 * receiver finishes with it), the underlying entity is automatically released
 * back to the pool for reuse.
 *
 * This enables true entity reuse in pipelines without manual release() calls.
 */
class PooledEntity {
 public:
  PooledEntity() = default;
  PooledEntity(Entity entity, std::shared_ptr<EntityPool> pool);
  ~PooledEntity();

  // Move-only (like Entity)
  PooledEntity(const PooledEntity&) = delete;
  PooledEntity& operator=(const PooledEntity&) = delete;
  PooledEntity(PooledEntity&& other) noexcept;
  PooledEntity& operator=(PooledEntity&& other) noexcept;

  // Access the underlying entity
  Entity& get() { return entity_; }
  const Entity& get() const { return entity_; }
  Entity* operator->() { return &entity_; }
  const Entity* operator->() const { return &entity_; }

  // Check if valid
  bool valid() const { return !entity_.is_null(); }
  explicit operator bool() const { return valid(); }

  // Get the underlying entity (transfers ownership, pool won't reclaim)
  Entity release();

  // Get entity ID
  gxf_uid_t eid() const { return entity_.eid(); }

 private:
  Entity entity_;
  std::shared_ptr<EntityPool> pool_;
};

/**
 * @brief A high-performance pool for reusing Entity objects.
 *
 * EntityPool eliminates the overhead of repeated entity creation and destruction
 * by maintaining a pool of pre-allocated entities that can be acquired and released.
 * This is particularly beneficial for high-throughput message passing scenarios
 * where entities are created/destroyed frequently.
 *
 * Key features:
 * - Mutex-protected free list for thread-safe acquire/release
 * - Automatic entity return via PooledEntity RAII wrapper
 * - Components are cleared on release, EntityItem is preserved
 * - Automatic fallback to new entity creation if pool is exhausted
 *
 * Pool growth behaviour:
 * When all pre-allocated slots are in use, acquire() creates additional entities
 * on demand and tracks them so they are returned to the pool when released.
 * The pool therefore grows beyond its initial capacity and never shrinks.
 * In bursty workloads this can lead to gradual memory accumulation.
 * Sizing the pool to cover peak in-flight demand reduces overflow allocations
 * but does not enforce a hard upper bound on pool size.
 *
 * Sizing recommendation:
 * - Set pool_size >= num_pipelines * chain_length for best results
 * - Pool size should cover the number of entities in-flight simultaneously
 *
 * Usage:
 * @code
 *   // Create a pool with 100 pre-allocated entities
 *   auto pool = EntityPool::Create(context, 100);
 *
 *   // Acquire a pooled entity (auto-releases when destroyed)
 *   auto pooled = pool->acquirePooled();
 *
 *   // Use the entity...
 *   pooled->add<MyComponent>("data");
 *
 *   // Publish - entity flows through pipeline
 *   transmitter->publish(pooled.get());
 *
 *   // When PooledEntity is destroyed anywhere in the pipeline,
 *   // the entity automatically returns to the pool
 * @endcode
 */
class EntityPool {
 public:
  /**
   * @brief Creates a new entity pool with the specified capacity.
   *
   * @param context Valid GXF context
   * @param capacity Initial number of entities to pre-allocate
   * @return Expected<std::shared_ptr<EntityPool>> Pool instance or error
   */
  static Expected<std::shared_ptr<EntityPool>> Create(gxf_context_t context, size_t capacity);

  /**
   * @brief Gets or creates a shared pool for the given context.
   *
   * All callers with the same context will receive the same pool instance.
   * This is more efficient than per-component pools because entities can be
   * returned and reused by ANY component, not just the one that created them.
   *
   * @param context Valid GXF context
   * @param capacity Initial capacity (only used if pool doesn't exist yet)
   * @return std::shared_ptr<EntityPool> Shared pool instance
   */
  static std::shared_ptr<EntityPool> GetShared(gxf_context_t context, size_t capacity = 64);

  ~EntityPool();

  // Non-copyable, non-movable (shared_ptr handles ownership)
  EntityPool(const EntityPool&) = delete;
  EntityPool& operator=(const EntityPool&) = delete;
  EntityPool(EntityPool&&) = delete;
  EntityPool& operator=(EntityPool&&) = delete;

  /**
   * @brief Acquires an entity from the pool (raw, no auto-release).
   *
   * If the pool has available entities, returns one immediately.
   * If the pool is empty, creates a new entity.
   * Note: Caller is responsible for calling release() when done.
   *
   * @return Expected<Entity> Entity ready for use, or error
   */
  Expected<Entity> acquire();

  /**
   * @brief Acquires a pooled entity with automatic release on destruction.
   *
   * This is the preferred method for pipeline use. The returned PooledEntity
   * will automatically release the underlying entity back to the pool when
   * destroyed, enabling true entity reuse.
   *
   * @param self shared_ptr to this pool (needed for destructor callback)
   * @return Expected<PooledEntity> Pooled entity ready for use, or error
   */
  Expected<PooledEntity> acquirePooled(std::shared_ptr<EntityPool> self);

  /**
   * @brief Releases an entity back to the pool by EID.
   *
   * The entity's components are cleared and it's returned to the pool
   * for future reuse. This is the preferred method when you have the EID.
   *
   * @param eid Entity ID to release
   * @return GXF_SUCCESS on success
   */
  gxf_result_t release(gxf_uid_t eid);

  /**
   * @brief Releases an entity back to the pool.
   *
   * The entity's components are cleared and it's returned to the pool
   * for future reuse. The entity handle becomes invalid after this call.
   *
   * @param entity Entity to release (will be consumed)
   * @return GXF_SUCCESS on success
   */
  gxf_result_t release(Entity&& entity);

  /**
   * @brief Checks if an entity belongs to this pool.
   *
   * @param eid Entity ID to check
   * @return true if the entity was created by or added to this pool
   */
  bool isPoolEntity(gxf_uid_t eid) const;

  /**
   * @brief Unregisters this pool from global registry.
   */
  void unregisterGlobally();

  /**
   * @brief Checks if an entity belongs to ANY registered pool and returns it.
   *
   * Called from GXF runtime when an entity would be destroyed.
   * @param context GXF context
   * @param eid Entity ID to check
   * @return true if entity was returned to a pool, false if should be destroyed normally
   */
  static bool TryReturnToPool(gxf_context_t context, gxf_uid_t eid);

  /**
   * @brief Tries to acquire a recycled entity from the global pool.
   *
   * Called from Runtime::GxfCreateEntity for unnamed, non-program entities.
   * On first call, lazily creates the pool using GXF_ENTITY_POOL_SIZE env var
   * (default 256, set to 0 to disable).
   *
   * Returns raw eid/item_ptr with refcount unchanged (stays at 0 in pool).
   * The caller is expected to use Entity::Shared() to increment refcount.
   *
   * @param context GXF context
   * @param eid [out] Entity ID of the acquired entity
   * @param item_ptr [out] Pointer to EntityItem
   * @return true if an entity was acquired from the pool, false otherwise
   */
  static bool TryAcquireFromPool(gxf_context_t context, gxf_uid_t& eid, void** item_ptr);

  /**
   * @brief Releases global/shared pool references for a context.
   *
   * Ensures global pool lifetimes do not outlive the associated GXF context.
   * Safe to call multiple times.
   */
  static void ReleaseContextPools(gxf_context_t context);

  /**
   * @brief Returns the number of available entities in the pool.
   */
  int64_t available() const;

  /**
   * @brief Returns the total capacity of the pool.
   */
  size_t capacity() const;

  /**
   * @brief Returns the number of entities currently in use (acquired but not released).
   */
  size_t in_use() const;

  /**
   * @brief Returns statistics about pool usage.
   */
  struct Stats {
    size_t total_acquires{0};
    size_t total_releases{0};
    size_t pool_hits{0};      // Acquires satisfied from pool
    size_t pool_misses{0};    // Acquires that needed new entity creation
    int64_t current_available{0};
    size_t current_in_use{0};
  };
  Stats stats() const;

 private:
  // Private constructor - use Create()
  explicit EntityPool(gxf_context_t context);

  // Pre-allocate entities into the pool
  gxf_result_t preallocate(size_t count);

  // Clear all components from an entity (prepare for reuse).
  // When item_ptr is non-null, uses direct path that bypasses shared_mutex_ acquisition.
  gxf_result_t clearEntity(gxf_uid_t eid, void* item_ptr = nullptr);

  // Implementation detail of this build: destroys all entities currently
  // sitting in the free list (idempotent). Called eagerly by
  // ReleaseContextPools while the context is valid, and by the destructor as
  // a fallback.
  void destroyFreeEntities();

  // Internal structure for pool entries
  struct PoolEntry {
    gxf_uid_t eid{kNullUid};
    void* entity_item_ptr{nullptr};
    // Implementation detail of this build: guards the free list against
    // double-returns (RAII release + runtime return hook may both fire).
    bool in_free_list{true};
  };

  gxf_context_t context_;

  // Mutex-protected free list for available entities.
  // Previous implementation used a lock-free Treiber stack which suffered from the ABA
  // problem under concurrent acquire/release (see GXF_ENTITY_POOL_ABA_BUG.md).
  // A mutex is used instead because the acquire/release cost is dominated by downstream
  // operations (component allocation, warden locks) making lock-free overhead negligible.
  std::vector<PoolEntry*> free_entries_;
  mutable std::mutex free_list_mutex_;  // Protects free_entries_

  // All pool entries (for cleanup)
  std::vector<std::unique_ptr<PoolEntry>> all_entries_;
  std::unordered_set<gxf_uid_t> pool_eids_;  // Track which EIDs belong to pool
  std::unordered_map<gxf_uid_t, PoolEntry*> eid_to_entry_;  // O(1) EID -> entry lookup
  mutable std::mutex entries_mutex_;  // Protects all_entries_, pool_eids_, eid_to_entry_

  // Statistics
  std::atomic<size_t> total_acquires_{0};
  std::atomic<size_t> total_releases_{0};
  std::atomic<size_t> pool_hits_{0};      // Acquires satisfied from pool
  std::atomic<size_t> pool_misses_{0};    // Acquires that fell back to new entity creation
  std::atomic<int64_t> available_count_{0};

  // Set by ReleaseContextPools before dropping the shared_ptr.
  // When true, the destructor knows the context is still valid and can
  // safely call GxfEntityDestroy.  When false (e.g. static destruction
  // of g_global_pool_refs after the context has been freed), the
  // destructor skips GxfEntityDestroy to avoid use-after-free.
  bool properly_released_{false};
};

}  // namespace gxf
}  // namespace nvidia

#endif  // NVIDIA_GXF_STD_ENTITY_POOL_HPP_
