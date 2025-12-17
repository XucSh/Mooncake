#include <glog/logging.h>
#include <algorithm>
#include <vector>
#include <limits>

#include "tiered_cache/tiered_backend.h"
#include "tiered_cache/cache_tier.h"

namespace mooncake {

/**
 * @brief Destructor that releases the allocation's backing resource.
 *
 * If this entry has an associated backend, invokes backend->FreeInternal(loc)
 * to free the physical resource when the allocation's lifetime ends (reference
 * count drops to zero).
 */
AllocationEntry::~AllocationEntry() {
    if (backend) {
        // When ref count drops to 0, call back to backend to free physical
        // resource.
        backend->FreeInternal(loc);
    }
}

/**
 * @brief Constructs a TieredBackend with default-initialized internal state.
 *
 * Initializes the backend object with its members set to their default values.
 */
TieredBackend::TieredBackend() = default;

/**
 * @brief Initializes the backend by constructing the DataCopier and loading tier metadata from JSON.
 *
 * Parses the provided JSON `root` for a "tiers" array and records per-tier metadata (id, priority, tags).
 * Also constructs the internal DataCopier instance. Actual instantiation of CacheTier objects is
 * left to the tier creation logic (placeholder in this implementation).
 *
 * @param root JSON object containing a "tiers" member: each entry must include an `id` and `priority`,
 *             and may include an optional `tags` array of strings.
 * @param engine TransferEngine pointer intended for use when initializing concrete tier instances.
 * @return true if initialization completed successfully and tier metadata was loaded; `false` if the
 *         JSON is missing the required "tiers" member or DataCopier construction failed.
 */
bool TieredBackend::Init(Json::Value root, TransferEngine* engine) {
    // Initialize DataCopier
    try {
        DataCopierBuilder builder;
        data_copier_ = builder.Build();
    } catch (const std::logic_error& e) {
        LOG(FATAL) << "Failed to build DataCopier: " << e.what();
        return false;
    }

    // Initialize Tiers
    if (!root.isMember("tiers")) {
        LOG(ERROR) << "Tiered cache config is missing 'tiers' array.";
        return false;
    }

    for (const auto& tier_config : root["tiers"]) {
        uint64_t id = tier_config["id"].asUInt();
        // std::string type = tier_config["type"].asString(); // Unused for now
        int priority = tier_config["priority"].asInt();
        std::vector<std::string> tags;
        if (tier_config.isMember("tags")) {
            for (const auto& tag : tier_config["tags"])
                tags.push_back(tag.asString());
        }

        // TODO: Logic to instantiate specific CacheTier types (DRAM/SSD) goes
        // here. For example: std::unique_ptr<CacheTier> tier =
        // CacheTierFactory::Create(tier_config); tier->Init(this, engine);
        // tiers_[id] = std::move(tier);

        // Placeholder for compilation if Factory is not ready
        // tiers_[id] = std::make_unique<DramTier>();

        tier_info_[id] = {priority, tags};
    }

    LOG(INFO) << "TieredBackend initialized successfully with " << tiers_.size()
              << " tiers.";
    return true;
}

/**
 * @brief Get tier IDs ordered by priority.
 *
 * Returns the list of configured tier identifiers sorted by priority with higher-priority
 * tiers appearing earlier in the vector.
 *
 * @return std::vector<uint64_t> Vector of tier IDs ordered by descending priority (highest priority first). If two tiers have the same priority, their relative order is unspecified.
 */
std::vector<uint64_t> TieredBackend::GetSortedTiers() const {
    std::vector<uint64_t> ids;
    for (const auto& [id, _] : tiers_) ids.push_back(id);

    // Sort by priority descending (higher priority first)
    std::sort(ids.begin(), ids.end(), [this](uint64_t a, uint64_t b) {
        return tier_info_.at(a).priority > tier_info_.at(b).priority;
    });
    return ids;
}

/**
 * Attempts to allocate storage for a buffer of the given size, preferring the optional
 * preferred_tier and falling back to other tiers ordered by priority.
 *
 * If allocation succeeds, populates `out_loc->tier_id` and `out_loc->data` with the
 * allocated location for the chosen tier.
 *
 * @param size Number of bytes to allocate.
 * @param preferred_tier Optional tier id to try first.
 * @param out_loc Output pointer that will be populated with the chosen tier and data on success.
 * @return true if allocation succeeded and `out_loc` was populated, false otherwise.
 */
bool TieredBackend::AllocateInternalRaw(size_t size,
                                        std::optional<uint64_t> preferred_tier,
                                        TieredLocation* out_loc) {
    if (!out_loc) return false;

    // Try preferred tier first
    if (preferred_tier.has_value()) {
        auto it = tiers_.find(*preferred_tier);
        if (it != tiers_.end()) {
            if (it->second->Allocate(size, out_loc->data)) {
                out_loc->tier_id = *preferred_tier;
                return true;
            }
        }
    }

    // Fallback: Auto-tiering based on priority
    auto sorted_tiers = GetSortedTiers();
    for (uint64_t tier_id : sorted_tiers) {
        if (preferred_tier.has_value() && tier_id == *preferred_tier) continue;

        auto& tier = tiers_[tier_id];
        if (tier->Allocate(size, out_loc->data)) {
            out_loc->tier_id = tier_id;
            return true;
        }
    }
    return false;
}

/**
 * @brief Frees the storage referenced by a TieredLocation from its associated tier if that tier exists.
 *
 * @param loc Location that specifies the target tier (`tier_id`) and the tier-local allocation (`data`) to free.
 */
void TieredBackend::FreeInternal(const TieredLocation& loc) {
    auto it = tiers_.find(loc.tier_id);
    if (it != tiers_.end()) {
        it->second->Free(loc.data);
    }
}

/**
 * @brief Allocates storage from the tiered backend and returns a handle to it.
 *
 * Attempts to allocate `size` bytes, preferring `preferred_tier` when provided;
 * falls back to automatic tier selection if the preferred tier cannot satisfy the request.
 *
 * @param size Number of bytes to allocate.
 * @param preferred_tier Optional id of the tier to prefer for the allocation.
 * @return AllocationHandle Shared handle to the allocated location (reference count = 1) on success, `nullptr` on failure.
 *
 * @note If the returned handle is destroyed without being committed, the underlying allocation is freed.
 */
AllocationHandle TieredBackend::Allocate(
    size_t size, std::optional<uint64_t> preferred_tier) {
    TieredLocation loc;
    if (AllocateInternalRaw(size, preferred_tier, &loc)) {
        // Create the handle (Ref count = 1).
        // If this handle dies without being committed, AllocationEntry
        // destructor triggers FreeInternal.
        return std::make_shared<AllocationEntry>(this, loc);
    }
    return nullptr;
}

/**
 * @brief Writes the provided data source into the allocation specified by the handle.
 *
 * @param source Data to write into the allocation.
 * @param handle Target allocation handle whose location determines the destination tier; must not be null.
 * @return true if the data was successfully copied into the allocation; false if `handle` is null, the target tier is missing, or the copy operation fails.
 */
bool TieredBackend::Write(const DataSource& source, AllocationHandle handle) {
    if (!handle) return false;
    auto it = tiers_.find(handle->loc.tier_id);
    if (it == tiers_.end()) return false;

    return data_copier_->Copy(source, handle->loc.data);
}

/**
 * @brief Store or update a replica for a metadata key using the provided allocation handle.
 *
 * Associates the given handle's tier with the metadata key: if a replica for the same
 * tier exists it is replaced; otherwise a new replica is appended and replicas are
 * ordered by tier priority (higher priority first).
 *
 * @param key Metadata key to commit the replica under.
 * @param handle Allocation handle containing the tier and location to commit. If null, no action is taken.
 * @return bool `true` if the replica was committed or updated, `false` if `handle` is null.
 */
bool TieredBackend::Commit(const std::string& key, AllocationHandle handle) {
    if (!handle) return false;

    std::shared_ptr<MetadataEntry> entry = nullptr;

    // Try to find existing entry (Global Read Lock)
    {
        std::shared_lock<std::shared_mutex> read_lock(map_mutex_);
        auto it = metadata_index_.find(key);
        if (it != metadata_index_.end()) {
            entry = it->second;
        }
    }

    // Create if not exists (Global Write Lock)
    if (!entry) {
        std::unique_lock<std::shared_mutex> write_lock(map_mutex_);
        // Double-check logic
        auto it = metadata_index_.find(key);
        if (it != metadata_index_.end()) {
            entry = it->second;
        } else {
            entry = std::make_shared<MetadataEntry>();
            metadata_index_[key] = entry;
        }
    }

    //  Update Entry (Entry Write Lock)
    // Global lock is released. We only lock this specific key's entry.
    {
        std::unique_lock<std::shared_mutex> entry_lock(entry->mutex);
        // Insert or replace the handle for this tier
        bool found = false;
        for (auto& replica : entry->replicas) {
            if (replica.first == handle->loc.tier_id) {
                replica.second = handle;
                found = true;
                break;
            }
        }

        if (!found) {
            entry->replicas.emplace_back(handle->loc.tier_id, handle);
            std::sort(entry->replicas.begin(), entry->replicas.end(),
                      [this](const std::pair<uint64_t, AllocationHandle>& a,
                             const std::pair<uint64_t, AllocationHandle>& b) {
                          return tier_info_.at(a.first).priority >
                                 tier_info_.at(b.first).priority;
                      });
        }
    }

    return true;
}

/**
 * @brief Retrieve the allocation handle for a metadata key, optionally constrained to a specific tier.
 *
 * Searches the metadata index for the given key and returns the corresponding replica's AllocationHandle.
 *
 * @param key Metadata key to look up.
 * @param tier_id Optional tier identifier to select a replica from that tier.
 * @return AllocationHandle The handle for the found replica, or `nullptr` if the key or requested replica is not present. If `tier_id` is provided, returns the replica from that tier or `nullptr` if none exists; if not provided, returns the highest-priority replica for the key or `nullptr` if there are no replicas.
 */
AllocationHandle TieredBackend::Get(const std::string& key,
                                    std::optional<uint64_t> tier_id) {
    std::shared_ptr<MetadataEntry> entry = nullptr;

    // Find Entry (Global Read Lock)
    {
        std::shared_lock<std::shared_mutex> read_lock(map_mutex_);
        auto it = metadata_index_.find(key);
        if (it == metadata_index_.end()) {
            return nullptr;
        }
        entry = it->second;
    }

    // Read Entry (Entry Read Lock)
    std::shared_lock<std::shared_mutex> entry_read_lock(entry->mutex);

    if (entry->replicas.empty()) return nullptr;

    if (tier_id.has_value()) {
        for (const auto& replica : entry->replicas) {
            if (replica.first == *tier_id) {
                return replica.second;
            }
        }
        return nullptr;
    }

    // Fallback: Return highest priority replica
    return entry->replicas.begin()->second;
}

/**
 * @brief Remove one or all replicas for a metadata key.
 *
 * If `tier_id` is provided, removes only the replica stored in that tier.
 * If `tier_id` is not provided, removes all replicas and erases the key.
 *
 * @param key Metadata key whose replica(s) should be removed.
 * @param tier_id Optional tier identifier specifying a single replica to delete.
 * @return true If a replica was removed (when `tier_id` is provided) or if the key existed and was deleted (when `tier_id` is not provided); `false` if no matching replica/key was found.
 *
 * @note Resource handles for removed replicas are released after locks are dropped so actual freeing of underlying storage occurs outside of held locks.
 */
bool TieredBackend::Delete(const std::string& key,
                           std::optional<uint64_t> tier_id) {
    // Hold references locally to ensure destruction happens OUTSIDE the
    // locks This is crucial for non-blocking deletions.
    AllocationHandle handle_ref = nullptr;
    std::vector<AllocationHandle> handles_to_free;

    if (tier_id.has_value()) {
        // Delete Specific Replica

        bool need_cleanup = false;
        bool found_tier = false;

        // Optimistic Delete (Global Read Lock + Entry Write Lock)
        // This is fast and allows high concurrency.
        {
            std::shared_lock<std::shared_mutex> read_lock(map_mutex_);
            auto it = metadata_index_.find(key);
            if (it != metadata_index_.end()) {
                auto entry = it->second;

                std::unique_lock<std::shared_mutex> entry_write_lock(
                    entry->mutex);
                auto tier_it = entry->replicas.end();
                for (auto it = entry->replicas.begin();
                     it != entry->replicas.end(); ++it) {
                    if (it->first == *tier_id) {
                        tier_it = it;
                        break;
                    }
                }

                if (tier_it != entry->replicas.end()) {
                    handle_ref =
                        tier_it->second;  // Capture reference (+1 ref count)
                    entry->replicas.erase(tier_it);
                    found_tier = true;
                }

                // Mark for cleanup if entry becomes empty
                if (entry->replicas.empty()) {
                    need_cleanup = true;
                }
            }
        }  // Read lock released here

        // Retry with Write Lock
        // If the entry is empty, we upgrade to a global write lock to remove
        // it. This prevents memory leaks from empty "zombie" entries.
        if (need_cleanup) {
            std::unique_lock<std::shared_mutex> write_lock(map_mutex_);

            auto it = metadata_index_.find(key);
            if (it != metadata_index_.end()) {
                auto entry = it->second;

                // Double-Check Locking:
                // Another thread might have added a replica now
                std::unique_lock<std::shared_mutex> entry_lock(entry->mutex);

                if (entry->replicas.empty()) {
                    // Confirmed empty, safe to remove from global index
                    metadata_index_.erase(it);
                }
            }
        }

        return found_tier;

    } else {
        // Delete All Replicas (Full Key Deletion)
        // Requires Global Write Lock since we are modifying the map structure.
        std::unique_lock<std::shared_mutex> global_write_lock(map_mutex_);
        auto it = metadata_index_.find(key);
        if (it == metadata_index_.end()) return false;

        auto entry = it->second;

        {
            std::unique_lock<std::shared_mutex> entry_lock(entry->mutex);
            handles_to_free.reserve(entry->replicas.size());
            for (auto& replica : entry->replicas) {
                handles_to_free.push_back(replica.second);
            }
            entry->replicas.clear();
        }

        // Remove the entry from the global index
        metadata_index_.erase(it);
    }

    // Handles go out of scope here.
    // Ref count drops to 0 -> ~AllocationEntry() -> FreeInternal().
    // This happens concurrently without holding any locks.
    return true;
}

/**
 * @brief Copies data into a destination tier and registers it as a replica for a key.
 *
 * Allocates space in the specified destination tier, writes the provided data into that allocation,
 * optionally synchronizes the new location with an external master via the provided callback,
 * and on success adds the allocation as a replica for the given key.
 *
 * @param key Metadata key under which the new replica will be committed.
 * @param source Source data and size to copy; `source.size` must be greater than zero.
 * @param dest_tier_id Tier identifier where the data should be allocated and written.
 * @param sync_cb Optional callback invoked as `sync_cb(key, loc)` to synchronize the new location with a master; if provided, the operation aborts when the callback returns `false`.
 * @return true if the data was copied, optionally synchronized, and committed as a replica; `false` on any failure (allocation, write, sync callback failure, or commit).
 */
bool TieredBackend::CopyData(const std::string& key, const DataSource& source,
                             uint64_t dest_tier_id,
                             MetadataSyncCallback sync_cb) {
    if (source.size == 0) return false;
    auto dest_handle = Allocate(source.size, dest_tier_id);
    if (!dest_handle) return false;

    if (!Write(source, dest_handle)) return false;

    // Sync with Master (Critical Step)
    if (sync_cb) {
        bool sync_success = sync_cb(key, dest_handle->loc);
        if (!sync_success) {
            LOG(ERROR) << "CopyData aborted: Master sync failed for key "
                       << key;
            return false;
        }
    }

    // Commit (Add Replica)
    // Takes ownership of dest_handle into the map
    return Commit(key, dest_handle);
}

/**
 * @brief Builds a snapshot of each configured tier's metrics and metadata.
 *
 * @return std::vector<TierView> A vector of TierView objects, one per configured tier.
 * Each TierView contains the tier id, memory type, total capacity, used bytes,
 * available bytes (capacity minus used), priority, and associated tags.
 */
std::vector<TierView> TieredBackend::GetTierViews() const {
    std::vector<TierView> views;
    for (const auto& [id, tier] : tiers_) {
        const auto& info = tier_info_.at(id);
        size_t cap = tier->GetCapacity();
        size_t used = tier->GetUsage();
        views.push_back({id, tier->GetMemoryType(), cap, used, cap - used,
                         info.priority, info.tags});
    }
    return views;
}

/**
 * Retrieve the cache tier associated with the given tier identifier.
 *
 * The returned pointer is owned by the TieredBackend and remains valid while
 * this backend instance exists and the tier is not removed.
 *
 * @param tier_id Identifier of the tier to look up.
 * @return const CacheTier* Pointer to the CacheTier if found, `nullptr` otherwise.
 */
const CacheTier* TieredBackend::GetTier(uint64_t tier_id) const {
    auto it = tiers_.find(tier_id);
    return (it != tiers_.end()) ? it->second.get() : nullptr;
}

/**
 * @brief Gets the configured DataCopier used for data transfers.
 *
 * @return const DataCopier& Reference to the backend's DataCopier instance.
 */
const DataCopier& TieredBackend::GetDataCopier() const { return *data_copier_; }

}  // namespace mooncake