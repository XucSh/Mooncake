#include "tiered_cache/copier_registry.h"
#include "tiered_cache/data_copier.h"
#include <utility>

namespace mooncake {

/**
 * @brief Provides access to the global CopierRegistry singleton.
 *
 * @return CopierRegistry& Reference to the shared CopierRegistry instance.
 */
CopierRegistry& CopierRegistry::GetInstance() {
    static CopierRegistry instance;
    return instance;
}

/**
 * @brief Registers a memory type and its associated copy callbacks with the registry.
 *
 * Adds an entry for `type` that associates a copy-to-DRAM callback and a copy-from-DRAM callback
 * so callers can look up copy functions for that memory type.
 *
 * @param type The memory type being registered.
 * @param to_dram Callback invoked to copy data from this memory type to DRAM.
 * @param from_dram Callback invoked to copy data from DRAM to this memory type.
 */
void CopierRegistry::RegisterMemoryType(MemoryType type, CopyFunction to_dram,
                                        CopyFunction from_dram) {
    memory_type_regs_.push_back(
        {type, std::move(to_dram), std::move(from_dram)});
}

/**
 * @brief Registers a direct copy path between two memory types.
 *
 * Adds a direct-path registration so copies can be performed directly from `src`
 * to `dest` using the provided callback.
 *
 * @param src Source memory type for the direct copy path.
 * @param dest Destination memory type for the direct copy path.
 * @param func Copy callback used to perform the direct copy; ownership of `func`
 * is transferred (moved) into the registry.
 */
void CopierRegistry::RegisterDirectPath(MemoryType src, MemoryType dest,
                                        CopyFunction func) {
    direct_path_regs_.push_back({src, dest, std::move(func)});
}

/**
 * @brief Get all registered memory type transfer registrations.
 *
 * @return const std::vector<MemoryTypeRegistration>& A const reference to the vector of `MemoryTypeRegistration` entries that have been registered.
 */
const std::vector<MemoryTypeRegistration>&
CopierRegistry::GetMemoryTypeRegistrations() const {
    return memory_type_regs_;
}

/**
 * @brief Provides access to the registered direct copy paths between memory types.
 *
 * @return const std::vector<DirectPathRegistration>& The collection of DirectPathRegistration entries representing direct copy callbacks for specific source→destination memory type pairs.
 */
const std::vector<DirectPathRegistration>&
CopierRegistry::GetDirectPathRegistrations() const {
    return direct_path_regs_;
}

/**
 * @brief Registers a memory type and its to/from-DRAM copy callbacks with the global CopierRegistry.
 *
 * Constructing this registrar performs registration (suitable for static initialization).
 *
 * @param type The memory type being registered.
 * @param to_dram Callback used to copy data from the memory type to DRAM.
 * @param from_dram Callback used to copy data from DRAM to the memory type.
 */
CopierRegistrar::CopierRegistrar(MemoryType type, CopyFunction to_dram,
                                 CopyFunction from_dram) {
    // When a static CopierRegistrar object is created, it registers the memory
    // type.
    CopierRegistry::GetInstance().RegisterMemoryType(type, std::move(to_dram),
                                                     std::move(from_dram));
}

}  // namespace mooncake