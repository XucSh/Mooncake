import torch
import os
import sys
import numpy as np
from mooncake.store import MooncakeDistributedStore
from mooncake.mooncake_config import MooncakeConfig

def setup_store():
    store = MooncakeDistributedStore()
    # Mock config if env vars not set, or use defaults
    # Try to use existing config logic if possible, otherwise manual default
    try:
        config = MooncakeConfig.load_from_env()
    except Exception:
        # Fallback defaults
        class Config:
            pass
        config = Config()
        config.local_hostname = "localhost"
        config.metadata_server = "127.0.0.1:2379"
        config.global_segment_size = 1024 * 1024 * 1024
        config.local_buffer_size = 1024 * 1024 * 1024
        config.protocol = "tcp"
        config.device_name = "lo0" # use loopback if needed
        config.master_server_address = "127.0.0.1:50051"

    print(f"Connecting to Mooncake Master at {config.master_server_address}...")
    rc = store.setup(
        config.local_hostname,
        config.metadata_server,
        config.global_segment_size,
        config.local_buffer_size,
        config.protocol,
        config.device_name,
        config.master_server_address,
    )
    if rc != 0:
        print(f"Failed to setup mooncake store, error code: {rc}")
        sys.exit(1)
    return store

def test_non_contiguous_dense():
    print("\n--- Testing Non-Contiguous Dense Tensor ---")
    store = setup_store()
    
    # Create a tensor and slice it to be non-contiguous
    # A = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
    # B = A[::2] = [0, 2, 4, 6, 8]
    full_tensor = torch.arange(20, dtype=torch.int32)
    # Stride of full_tensor is (1,)
    
    non_contig = full_tensor[::2] 
    # Stride of non_contig is (2,)
    # Storage contains 0,1,2,3... but we only view 0,2,4...
    
    print(f"Original shape: {full_tensor.shape}")
    print(f"Sliced shape: {non_contig.shape}")
    print(f"Is contiguous? {non_contig.is_contiguous()}")
    
    key = "test_non_contig"
    
    print(f"Putting tensor {key}...")
    rc = store.put_tensor(key, non_contig)
    if rc != 0:
        print(f"Put failed with rc={rc}")
        return

    print(f"Getting tensor {key}...")
    retrieved = store.get_tensor(key)
    
    print(f"Original: {non_contig}")
    print(f"Retrieved: {retrieved}")
    
    if torch.equal(non_contig, retrieved):
        print("✅ SUCCESS: Tensors match!")
    else:
        print("❌ FAILURE: Tensors do not match!")
        # If bug exists, retrieved will likely contain [0, 1, 2, 3, 4] instead of [0, 2, 4, 6, 8]
        # because it reads contiguous memory from data_ptr
        expected_if_bug = full_tensor[:10] # or similar depending on implementation
        print(f"Expected if bug (roughly): {full_tensor[0:len(non_contig)]}")

def test_sparse_tensor_simulation():
    print("\n--- Testing Sparse Tensor Simulation ---")
    # Simulate what _put_coo_tensor_with_tp likely does with indices
    store = setup_store()
    
    # Random sparse tensor properties
    nnz = 5
    shape = (10, 10)
    indices = torch.tensor([[0, 2, 4, 6, 8], [1, 3, 5, 7, 9]], dtype=torch.int32)
    values = torch.randn(nnz)
    sparse = torch.sparse_coo_tensor(indices, values, shape)
    
    # If we take indices, it gives (2, 5) tensor.
    # indices[0] is [0, 2, 4, 6, 8] contiguous.
    # indices[:, 0] is [0, 1] non-contiguous (stride).
    
    # But usually we transfer row indices and col indices.
    # If we transfer `indices` as a whole, it is contiguous.
    # BUT, if we split it for TP (tensor parallelism)?
    
    # Suppose we split by rows (dim 0 of sparse tensor)
    # indices are filtered.
    
    # Let's verify standard put/get of the whole sparse tensor indices
    key_indices = "sparse_indices"
    rc = store.put_tensor(key_indices, indices)
    got_indices = store.get_tensor(key_indices)
    
    if torch.equal(indices, got_indices):
        print("✅ Indices match (contiguous case)")
    else:
        print("❌ Indices mismatch (contiguous case)")
        
    # Now simulate a non-contiguous slice of indices
    # e.g. indices[:, ::2] -> every second non-zero element
    nc_indices = indices[:, ::2]
    print(f"Non-contiguous indices shape: {nc_indices.shape}, is_contiguous: {nc_indices.is_contiguous()}")
    
    key_nc = "sparse_indices_nc"
    store.put_tensor(key_nc, nc_indices)
    got_nc = store.get_tensor(key_nc)
    
    print(f"Original NC: \n{nc_indices}")
    print(f"Retrieved NC: \n{got_nc}")
    
    if torch.equal(nc_indices, got_nc):
        print("✅ NC Indices match")
    else:
        print("❌ NC Indices mismatch")

if __name__ == "__main__":
    test_non_contiguous_dense()
    test_sparse_tensor_simulation()
