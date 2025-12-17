#pragma once

#include "tiered_cache/cache_tier.h"
#include <functional>
#include <map>
#include <memory>
#include <glog/logging.h>
#include <stdexcept>
#include <vector>

/**
 * @brief Builder that constructs a validated DataCopier instance.
 *
 * Ensures that all MemoryType entries have required copy paths registered
 * (copies to and from DRAM) before producing an immutable DataCopier.
 */

/**
 * @brief Constructs a builder initialized from the global CopierRegistry.
 *
 * Copies all current registrations from the global registry into the
 * builder's internal copy matrix.
 */

/**
 * @brief Registers an explicit direct copy path between two memory types.
 *
 * The provided function will be preferred over a DRAM fallback for the
 * given src_type -> dest_type pair. Can be used to add optimized or
 * non-registered paths (e.g., for testing).
 * @return Reference to this builder for chaining.
 */

/**
 * @brief Builds and returns a validated, immutable DataCopier.
 *
 * Verifies that required copy paths exist for every MemoryType before
 * constructing the DataCopier.
 * @return A unique_ptr to the constructed DataCopier.
 * @throws std::logic_error If a required to/from DRAM copy function is missing.
 */

/**
 * @brief Utility that copies data between memory types, with DRAM fallback.
 *
 * Performs copies using registered direct paths when available; otherwise
 * falls back to a two-step copy via a temporary DRAM buffer.
 */

/**
 * @brief Copies data from src to dst.
 *
 * Attempts a direct copy for src.memory_type -> dst.memory_type; if no direct
 * copier is registered, performs a two-step copy via DRAM.
 * @param src Data source descriptor.
 * @param dst Data destination descriptor.
 * @return `true` if the copy succeeds, `false` otherwise.
 */

/**
 * @brief Locates a registered copy function for the specified memory types.
 *
 * @param src_type Source memory type.
 * @param dest_type Destination memory type.
 * @return The registered CopyFunction if found, or an empty std::function otherwise.
 */
namespace mooncake {

using CopyFunction =
    std::function<bool(const DataSource& src, const DataSource& dst)>;

class DataCopier;

/**
 * @brief A helper class to build a valid DataCopier.
 *
 * This builder enforces the rule that for any new memory type added,
 * its copy functions to and from DRAM *must* be provided via the
 * CopierRegistry.
 */
class DataCopierBuilder {
   public:
    /**
     * @brief Constructs a builder. It automatically pulls all existing
     * registrations from the global CopierRegistry.
     */
    DataCopierBuilder();

    /**
     * @brief (Optional) Registers a highly optimized direct copy path.
     * This will be used instead of the DRAM fallback. Can be used for testing
     * or for paths that are not self-registered.
     * @return A reference to the builder for chaining.
     */
    DataCopierBuilder& AddDirectPath(MemoryType src_type, MemoryType dest_type,
                                     CopyFunction func);

    /**
     * @brief Builds the final, immutable DataCopier object.
     * It verifies that all memory types defined in the MemoryType enum
     * have been registered via the registry before creating the object.
     * @return A unique_ptr to the new DataCopier.
     * @throws std::logic_error if a required to/from DRAM copier is missing.
     */
    std::unique_ptr<DataCopier> Build() const;

   private:
    std::map<std::pair<MemoryType, MemoryType>, CopyFunction> copy_matrix_;
};

/**
 * @brief A central utility for copying data between different memory types.
 * It supports a fallback mechanism via DRAM for any copy paths that are not
 * explicitly registered as a direct path.
 */
class DataCopier {
   public:
    // The constructor is private. Use DataCopierBuilder to create an instance.
    ~DataCopier() = default;
    DataCopier(const DataCopier&) = delete;
    DataCopier& operator=(const DataCopier&) = delete;

    /**
     * @brief Executes a copy from a source to a destination.
     * It first attempts to find a direct copy function (e.g., VRAM -> VRAM).
     * If not found, it automatically falls back to a two-step copy via a
     * temporary DRAM buffer (e.g., VRAM -> DRAM -> SSD).
     * @param src The data source descriptor.
     * @param dest_type The memory type of the destination.
     * @param dest_ptr A pointer to the destination (memory address, handle,
     * etc.).
     * @return True if the copy was successful, false otherwise.
     */
    bool Copy(const DataSource& src, const DataSource& dst) const;

   private:
    friend class DataCopierBuilder;  // Allow builder to access the constructor.
    DataCopier(
        std::map<std::pair<MemoryType, MemoryType>, CopyFunction> copy_matrix);

    CopyFunction FindCopier(MemoryType src_type, MemoryType dest_type) const;
    const std::map<std::pair<MemoryType, MemoryType>, CopyFunction>
        copy_matrix_;
};

}  // namespace mooncake