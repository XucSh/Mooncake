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
    print("\n--- Testing Sparse Tensor Simulation (User's Logic) ---")
    store = setup_store()
    
    # Simulate the user's logic exactly
    nnz = 5
    shape = (10, 10)
    # Ensure indices are not sorted to force coalesce to do something if needed,
    # though here we manually construct provided indices.
    indices = torch.tensor([[0, 2, 4, 6, 8], [1, 3, 5, 7, 9]], dtype=torch.int64) 
    values = torch.randn(nnz)
    
    # 1. Create Sparse Tensor
    coo = torch.sparse_coo_tensor(indices, values, shape)
    
    # 2. User's function logic: _put_coo_tensor_with_tp
    # assert coo.layout == torch.sparse_coo
    coo = coo.coalesce()
    
    # tp_size = 1 case (where the bug happens)
    shards = [(coo, None, None)]
    
    keys, tensors = [], []
    name = "test_sparse_user_logic"
    
    for tp, (sub, _rrange, _crange) in enumerate(shards):
        prefix = f"{name}.tp{tp}.coo"
        keys += [
            f"{prefix}.indices",
            f"{prefix}.values",
            f"{prefix}.shape",
        ]
        
        # User's exact lines:
        # sub.indices() returns a tensor. 
        # .to(torch.int32) creates a new tensor (usually contiguous by default if copy happens).
        # .contiguous() ensures it.
        
        # Scenario A: indices is LongTensor (int64). to(int32) -> copy -> contiguous.
        # Scenario B: indices is IntTensor (int32). to(int32) -> self (view?) -> contiguous?
        
        # Let's inspect what happens naturally
        sub_indices = sub.indices()
        print(f"Sub indices dtype: {sub_indices.dtype}, shape: {sub_indices.shape}, stride: {sub_indices.stride()}")
        
        # User's transformation
        t_indices = sub.indices().to(torch.int32).contiguous()
        t_values = sub.values().contiguous()
        t_shape = torch.tensor(list(sub.shape), dtype=torch.int32, device='cpu')
        
        tensors += [t_indices, t_values, t_shape]
        
        print(f"Transformed indices info: is_contig={t_indices.is_contiguous()}, stride={t_indices.stride()}, ptr={t_indices.data_ptr()}")

    # 3. Batch Put
    print("Putting tensors...")
    results = store.batch_put_tensor(keys, tensors)
    if any(r != 0 for r in results):
        print(f"Batch put failed: {results}")
        return

    # 4. Verify loop (mimicking verification)
    print("Verifying...")
    retrieved = store.batch_get_tensor(keys)
    
    # indices is at index 0
    orig_indices = tensors[0]
    got_indices = retrieved[0]
    
    print(f"Original Indices:\n{orig_indices}")
    print(f"Got Indices:\n{got_indices}")
    
    if torch.equal(orig_indices, got_indices):
        print("✅ Indices match!")
    else:
        print("❌ Indices mismatch!")
        # Debugging what we got
        diff = (orig_indices != got_indices)
        if diff.any():
            print(f"First mismatch at: {torch.where(diff)[0][0]}")

    # Additional check: what if we purposely make a non-contiguous int32 tensor and feed it?
    # This verifies if my fix in store_py.cpp is working generally.
    print("\n--- Testing General Non-Contiguous Safety ---")
    dense_nc = torch.arange(20, dtype=torch.int32)[::2] # [0, 2, 4...] stride 2
    key_nc = "dense_nc_test"
    store.put_tensor(key_nc, dense_nc)
    got_nc = store.get_tensor(key_nc)
    if torch.equal(dense_nc, got_nc):
        print("✅ Non-contiguous dense tensor handled correctly (C++ fix works)")
    else:
        print("❌ Non-contiguous dense tensor corrupted (C++ fix NOT working)")


if __name__ == "__main__":
    test_non_contiguous_dense()
    test_sparse_tensor_simulation()
