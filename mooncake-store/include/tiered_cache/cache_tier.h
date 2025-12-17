#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "transfer_engine.h"

/**
 * @enum MemoryType
 * @brief Physical storage medium type used by a cache tier.
 */

/**
 * @brief Convert a MemoryType value to its string representation.
 * @param type The MemoryType value to convert.
 * @returns The string name of `type` ("DRAM", "NVME", or "UNKNOWN").
 */

/**
 * @struct DataSource
 * @brief Describes a source or reservation for data transfer operations.
 *
 * Fields:
 * - `ptr`: pointer value (in-memory address) or file descriptor identifying the source.
 * - `offset`: byte offset within the source (for files/SSD ranges).
 * - `size`: size in bytes.
 * - `type`: physical memory type of the source.
 */

/**
 * @class CacheTier
 * @brief Abstract interface representing a single cache tier (for example DRAM or NVMe).
 */

/**
 * @brief Initialize the cache tier with its parent backend and transfer engine.
 * @returns `true` if initialization succeeds, `false` otherwise.
 */

/**
 * @brief Reserve free space of the given size without performing any data copy.
 * @param data Output parameter that will be populated with allocation details (ptr/offset/size/type).
 * @returns `true` if the allocation succeeds, `false` otherwise.
 */

/**
 * @brief Release or roll back a previously allocated reservation described by `data`.
 * @returns `true` if the free/rollback succeeds, `false` otherwise.
 */

/**
 * @brief Retrieve the identifier for this tier.
 * @returns The tier identifier.
 */

/**
 * @brief Retrieve the total capacity of the tier in bytes.
 * @returns The tier capacity in bytes.
 */

/**
 * @brief Retrieve the current used bytes within the tier.
 * @returns The tier usage in bytes.
 */

/**
 * @brief Retrieve the MemoryType of this tier.
 * @returns The tier's MemoryType.
 */

/**
 * @brief Retrieve metadata tags associated with this tier.
 * @returns A reference to the vector of tag strings.
 */
namespace mooncake {

struct DataSource;
enum class MemoryType;
class TieredBackend;

/**
 * @enum MemoryType
 * @brief Defines the physical storage medium type for a cache tier.
 */
enum class MemoryType { DRAM, NVME, UNKNOWN };

static inline std::string MemoryTypeToString(MemoryType type) {
    switch (type) {
        case MemoryType::DRAM:
            return "DRAM";
        case MemoryType::NVME:
            return "NVME";
        default:
            return "UNKNOWN";
    }
}

/**
 * @struct DataSource
 * @brief Describes a source of data for copy/write operations.
 */
struct DataSource {
    uint64_t ptr;     // Pointer to data (if in memory) / file descriptor
    uint64_t offset;  // Offset within the source (for files/SSDs)
    size_t size;      // Size in bytes
    MemoryType type;  // Source memory type
};

/**
 * @class CacheTier
 * @brief Abstract base class for a single tier (e.g., DRAM, SSD).
 * * Update: Supports decoupled Allocation/Write/Bind operations to allow
 * flexible placement strategies (Client-centric vs Master-centric).
 */
class CacheTier {
   public:
    virtual ~CacheTier() = default;

    /**
     * @brief Initializes the cache tier.
     */
    virtual bool Init(TieredBackend* backend, TransferEngine* engine) = 0;

    /**
     * @brief Reserve Space (Allocation)
     * Finds free space of `size` bytes. Does NOT copy data.
     * * @param size Bytes to allocate.
     * @param data DataSource struct to fill with allocation info.
     * @return true if allocation succeeds.
     */
    virtual bool Allocate(size_t size, DataSource& data) = 0;

    /**
     * @brief Free Space (Rollback/Cleanup)
     * Releases space at offset. Used when writes fail or explicitly freeing
     * anonymous blocks.
     */
    virtual bool Free(DataSource data) = 0;

    // --- Accessors & Metadata ---
    virtual uint64_t GetTierId() const = 0;
    virtual size_t GetCapacity() const = 0;
    virtual size_t GetUsage() const = 0;
    virtual MemoryType GetMemoryType() const = 0;
    virtual const std::vector<std::string>& GetTags() const = 0;

   protected:
    // A pointer to the parent backend, allowing tiers to access shared services
    // like the DataCopier.
    TieredBackend* backend_ = nullptr;
};

}  // namespace mooncake