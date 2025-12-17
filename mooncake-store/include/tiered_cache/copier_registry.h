#pragma once

#include "tiered_cache/cache_tier.h"
#include "tiered_cache/data_copier.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mooncake {

// Forward declaration from data_copier.h to avoid circular dependency
class DataCopierBuilder;

// Holds the registration information for a memory type.
struct MemoryTypeRegistration {
    MemoryType type;
    CopyFunction to_dram_func;
    CopyFunction from_dram_func;
};

// Holds the registration for an optimized direct path.
struct DirectPathRegistration {
    MemoryType src_type;
    MemoryType dest_type;
    CopyFunction func;
};

/**
 * Singleton registry that collects data copier functions for different memory
 * types and optional optimized direct copy paths.
 *
 * Modules register copy functions (typically via static initialization)
 * and DataCopierBuilder queries this registry to assemble a DataCopier.
 */
 
/**
 * Access the global CopierRegistry instance.
 *
 * @returns Reference to the global CopierRegistry instance.
 */
 
/**
 * Register the to-DRAM and from-DRAM copy functions for a memory type.
 *
 * @param type Memory type being registered.
 * @param to_dram Copy function that copies data from `type` to DRAM.
 * @param from_dram Copy function that copies data from DRAM to `type`.
 */
 
/**
 * Register an optimized direct copy function for copying between two memory
 * types.
 *
 * @param src Source memory type.
 * @param dest Destination memory type.
 * @param func Copy function that copies from `src` to `dest`.
 */
 
/**
 * Retrieve all registered memory type entries.
 *
 * @returns Reference to the vector of MemoryTypeRegistration entries.
 */
 
/**
 * Retrieve all registered direct path entries.
 *
 * @returns Reference to the vector of DirectPathRegistration entries.
 */
 
/**
 * Helper that registers a memory type's to/from DRAM copy functions during
 * static initialization when instantiated as a static object.
 */
 
/**
 * Construct a CopierRegistrar and register the provided copy functions.
 *
 * @param type Memory type to register.
 * @param to_dram Copy function that copies data from `type` to DRAM.
 * @param from_dram Copy function that copies data from DRAM to `type`.
 */
class CopierRegistry {
   public:
    /**
     * @brief Get the singleton instance of the registry.
     */
    static CopierRegistry& GetInstance();

    /**
     * @brief Registers the to/from DRAM copy functions for a memory type.
     */
    void RegisterMemoryType(MemoryType type, CopyFunction to_dram,
                            CopyFunction from_dram);

    /**
     * @brief Registers an optional, optimized direct copy path.
     */
    void RegisterDirectPath(MemoryType src, MemoryType dest, CopyFunction func);

    // These methods are used by the DataCopierBuilder to collect all
    /**
 * Retrieve registered memory-type copy registrations.
 *
 * @returns A reference to the vector of MemoryTypeRegistration entries stored
 *          in the registry.
 */
/**
 * Retrieve registered direct-copy path registrations.
 *
 * @returns A reference to the vector of DirectPathRegistration entries stored
 *          in the registry.
 */
/**
 * Register a memory type's to/from DRAM copy functions at static
 * initialization time.
 *
 * @param type The MemoryType being registered.
 * @param to_dram Copy function used to transfer data from the memory type to DRAM.
 * @param from_dram Copy function used to transfer data from DRAM to the memory type.
 */
    const std::vector<MemoryTypeRegistration>& GetMemoryTypeRegistrations()
        const;
    const std::vector<DirectPathRegistration>& GetDirectPathRegistrations()
        const;

   private:
    friend class DataCopierBuilder;

    CopierRegistry() = default;
    ~CopierRegistry() = default;
    CopierRegistry(const CopierRegistry&) = delete;
    CopierRegistry& operator=(const CopierRegistry&) = delete;

    std::vector<MemoryTypeRegistration> memory_type_regs_;
    std::vector<DirectPathRegistration> direct_path_regs_;
};

/**
 * @brief A helper class to automatically register copiers at static
 * initialization time.
 *
 * To register a new memory type, simply declare a static instance of this class
 * in the corresponding .cpp file, providing the type and its to/from DRAM
 * copiers.
 */
class CopierRegistrar {
   public:
    CopierRegistrar(MemoryType type, CopyFunction to_dram,
                    CopyFunction from_dram);
};

}  // namespace mooncake