#include "tiered_cache/data_copier.h"
#include "tiered_cache/copier_registry.h"
#include <fstream>
#include <memory>
#include <utility>

namespace mooncake {

/**
 * @brief Initializes the builder's copy matrix from the global CopierRegistry.
 *
 * Populates the internal copy_matrix_ by registering, for each memory-type registration,
 * the type->DRAM and DRAM->type copy functions, and by registering each direct-path
 * (src_type -> dest_type) copy function from the registry.
 */
DataCopierBuilder::DataCopierBuilder() {
    // Process all registrations from the global registry.
    const auto& registry = CopierRegistry::GetInstance();

    for (const auto& reg : registry.GetMemoryTypeRegistrations()) {
        copy_matrix_[{reg.type, MemoryType::DRAM}] = reg.to_dram_func;
        copy_matrix_[{MemoryType::DRAM, reg.type}] = reg.from_dram_func;
    }
    for (const auto& reg : registry.GetDirectPathRegistrations()) {
        copy_matrix_[{reg.src_type, reg.dest_type}] = reg.func;
    }
}

/**
 * @brief Registers a direct copy function for a specific source→destination memory-type pair.
 *
 * Associates `func` with the (src_type, dest_type) pair so that direct copies between those
 * memory types will use the provided function.
 *
 * @param src_type Source memory type.
 * @param dest_type Destination memory type.
 * @param func Copy function to invoke for copies from `src_type` to `dest_type`.
 * @return DataCopierBuilder& Reference to this DataCopierBuilder.
 */
DataCopierBuilder& DataCopierBuilder::AddDirectPath(MemoryType src_type,
                                                    MemoryType dest_type,
                                                    CopyFunction func) {
    copy_matrix_[{src_type, dest_type}] = std::move(func);
    return *this;
}

/**
 * @brief Builds a configured DataCopier after validating required copy paths.
 *
 * Validates that for every registered memory type other than DRAM there exists
 * both a copy function from that type to DRAM and from DRAM to that type.
 * If validation succeeds, constructs and returns a DataCopier configured with
 * the builder's copy matrix.
 *
 * @return std::unique_ptr<DataCopier> Unique pointer owning the configured DataCopier.
 *
 * @throws std::logic_error If any non-DRAM memory type is missing a required
 * copy function to or from DRAM; the exception message identifies the missing pair.
 */
std::unique_ptr<DataCopier> DataCopierBuilder::Build() const {
    const auto& registry = CopierRegistry::GetInstance();
    for (const auto& reg : registry.GetMemoryTypeRegistrations()) {
        if (reg.type == MemoryType::DRAM) {
            continue;
        }
        if (copy_matrix_.find({reg.type, MemoryType::DRAM}) ==
            copy_matrix_.end()) {
            throw std::logic_error(
                "DataCopierBuilder Error: Missing copy function for type " +
                MemoryTypeToString(reg.type) + " TO DRAM.");
        }
        if (copy_matrix_.find({MemoryType::DRAM, reg.type}) ==
            copy_matrix_.end()) {
            throw std::logic_error(
                "DataCopierBuilder Error: Missing copy function for DRAM TO "
                "type " +
                MemoryTypeToString(reg.type) + ".");
        }
    }

    return std::unique_ptr<DataCopier>(new DataCopier(copy_matrix_));
}

/**
     * @brief Constructs a DataCopier with a predefined copy-function matrix.
     *
     * Initializes the DataCopier by taking ownership of a map that associates
     * (source memory type, destination memory type) pairs to their corresponding
     * copy functions.
     *
     * @param copy_matrix Map from (MemoryType, MemoryType) pairs to CopyFunction;
     *                    ownership of the map is transferred to the DataCopier.
     */
    DataCopier::DataCopier(
    std::map<std::pair<MemoryType, MemoryType>, CopyFunction> copy_matrix)
    : copy_matrix_(std::move(copy_matrix)) {}

/**
 * @brief Locate the registered copy function for a source-destination memory-type pair.
 *
 * @param src_type Source memory type.
 * @param dest_type Destination memory type.
 * @return CopyFunction The registered copy function for (src_type, dest_type) if one exists, `nullptr` otherwise.
 */
CopyFunction DataCopier::FindCopier(MemoryType src_type,
                                    MemoryType dest_type) const {
    auto it = copy_matrix_.find({src_type, dest_type});
    return (it != copy_matrix_.end()) ? it->second : nullptr;
}

/**
 * @brief Copies data from a source DataSource to a destination DataSource using registered copiers.
 *
 * Attempts a direct registered copier for (src.type -> dest.type). If no direct copier exists and
 * both source and destination are non-DRAM, attempts a two-step fallback via DRAM (src -> DRAM -> dest).
 *
 * @param src Source DataSource containing pointer, offset, size, and memory type.
 * @param dest Destination DataSource containing pointer, offset, size, and memory type.
 * @return true if the copy completed successfully; false if allocation fails, a copy step fails,
 * or no suitable copy path is registered.
 */
bool DataCopier::Copy(const DataSource& src, const DataSource& dest) const {
    MemoryType dest_type = dest.type;
    // Try to find a direct copy function.
    if (auto direct_copier = FindCopier(src.type, dest_type)) {
        VLOG(1) << "Using direct copier for " << MemoryTypeToString(src.type)
                << " -> " << MemoryTypeToString(dest_type);
        return direct_copier(src, dest);
    }

    // If no direct copier, try fallback via DRAM.
    if (src.type != MemoryType::DRAM && dest_type != MemoryType::DRAM) {
        VLOG(1) << "No direct copier. Attempting fallback via DRAM for "
                << MemoryTypeToString(src.type) << " -> "
                << MemoryTypeToString(dest_type);

        auto to_dram_copier = FindCopier(src.type, MemoryType::DRAM);
        auto from_dram_copier = FindCopier(MemoryType::DRAM, dest_type);

        if (to_dram_copier && from_dram_copier) {
            std::unique_ptr<char[]> temp_dram_buffer(
                new (std::nothrow) char[src.size]);
            if (!temp_dram_buffer) {
                LOG(ERROR) << "Failed to allocate temporary DRAM buffer for "
                              "fallback copy.";
                return false;
            }

            // Step A: Source -> DRAM
            DataSource temp_dram = {
                reinterpret_cast<uint64_t>(temp_dram_buffer.get()), 0, src.size,
                MemoryType::DRAM};
            if (!to_dram_copier(src, temp_dram)) {
                LOG(ERROR) << "Fallback copy failed at Step A (Source -> DRAM)";
                return false;
            }

            // Step B: DRAM -> Destination
            if (!from_dram_copier(temp_dram, dest)) {
                LOG(ERROR)
                    << "Fallback copy failed at Step B (DRAM -> Destination)";
                return false;
            }
            return true;
        }
    }

    LOG(ERROR) << "No copier registered for transfer from memory type "
               << MemoryTypeToString(src.type) << " to "
               << MemoryTypeToString(dest_type)
               << ", and fallback path is not available.";
    return false;
}

}  // namespace mooncake