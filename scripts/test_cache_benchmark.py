#!/usr/bin/env python3
"""
Cache-on-Get Benchmark Test

Usage:
  On writer machine:
    python test_cache_benchmark.py --role writer

  On reader machine:
    python test_cache_benchmark.py --role reader

Environment variables (set on both machines):
  MOONCAKE_MASTER=<master_ip>:50051
  MOONCAKE_LOCAL_HOSTNAME=<this_machine_ip>
  MOONCAKE_PROTOCOL=tcp          # or rdma
  MOONCAKE_DEVICE=""             # RDMA device if needed
  MOONCAKE_TE_META_DATA_SERVER=P2PHANDSHAKE
  MOONCAKE_GLOBAL_SEGMENT_SIZE=16gb
  MOONCAKE_LOCAL_BUFFER_SIZE=8gb
"""

import argparse
import ctypes
import os
import sys
import time

from mooncake.store import MooncakeDistributedStore
from mooncake.mooncake_config import MooncakeConfig


def create_store():
    """Create and initialize a MooncakeDistributedStore from env config."""
    config = MooncakeConfig.load_from_env()
    store = MooncakeDistributedStore()
    rc = store.setup(
        config.local_hostname,
        config.metadata_server,
        config.global_segment_size,
        config.local_buffer_size,
        config.protocol,
        config.device_name or "",
        config.master_server_address,
    )
    if rc != 0:
        raise RuntimeError(f"Failed to setup store, error code: {rc}")
    print(f"Store connected: hostname={config.local_hostname}, "
          f"master={config.master_server_address}, protocol={config.protocol}")
    return store


def run_writer(store, args):
    """Writer: put test data into the store."""
    size_bytes = args.size_mb * 1024 * 1024
    num_keys = args.num_keys

    print(f"\n=== Writer: putting {num_keys} keys, {args.size_mb} MB each ===")

    for i in range(num_keys):
        key = f"cache_bench_{i}"
        data = os.urandom(int(size_bytes))
        rc = store.put(key, data)
        if rc != 0:
            print(f"  put({key}) failed with rc={rc}")
            return
        print(f"  put({key}) OK, {args.size_mb} MB")

    print(f"\nWriter done. {num_keys} keys written.")
    print("Press Ctrl+C to exit (keep running so data stays alive)...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nWriter exiting.")


def bench_get_buffer(store, keys, cache, label):
    """Benchmark get_buffer for all keys, return total time in seconds."""
    # Warmup: single get to establish connection
    _ = store.get_buffer(keys[0], cache=False)

    start = time.perf_counter()
    for key in keys:
        buf = store.get_buffer(key, cache=cache)
        if buf is None:
            print(f"  ERROR: get_buffer({key}, cache={cache}) returned None")
            return -1
    elapsed = time.perf_counter() - start
    return elapsed


def bench_get_into(store, keys, size_bytes, cache, label):
    """Benchmark get_into for all keys using pre-registered buffer."""
    num_keys = len(keys)
    buf_size = int(size_bytes)
    total_size = buf_size * num_keys

    # Allocate and register a large buffer
    large_buf = (ctypes.c_ubyte * total_size)()
    large_ptr = ctypes.addressof(large_buf)
    rc = store.register_buffer(large_ptr, total_size)
    if rc != 0:
        print(f"  ERROR: register_buffer failed with rc={rc}")
        return -1, None, None

    # Warmup
    _ = store.get_into(keys[0], large_ptr, buf_size, cache=False)

    start = time.perf_counter()
    for i, key in enumerate(keys):
        ptr = large_ptr + i * buf_size
        nbytes = store.get_into(key, ptr, buf_size, cache=cache)
        if nbytes < 0:
            print(f"  ERROR: get_into({key}, cache={cache}) returned {nbytes}")
            store.unregister_buffer(large_ptr)
            return -1, None, None
    elapsed = time.perf_counter() - start

    store.unregister_buffer(large_ptr)
    return elapsed, large_buf, large_ptr


def run_reader(store, args):
    """Reader: benchmark get with and without cache."""
    size_bytes = args.size_mb * 1024 * 1024
    num_keys = args.num_keys
    keys = [f"cache_bench_{i}" for i in range(num_keys)]
    rounds = args.rounds

    print(f"\n=== Reader: benchmarking {num_keys} keys, "
          f"{args.size_mb} MB each, {rounds} rounds ===")

    # Verify keys exist
    for key in keys:
        buf = store.get_buffer(key, cache=False)
        if buf is None:
            print(f"  ERROR: key '{key}' not found. Is the writer running?")
            return
    print(f"  All {num_keys} keys verified.\n")

    total_mb = args.size_mb * num_keys

    # --- Benchmark 1: get_buffer without cache ---
    print(f"--- get_buffer (cache=False) x {rounds} rounds ---")
    for r in range(rounds):
        t = bench_get_buffer(store, keys, cache=False, label="no-cache")
        bw = total_mb / t if t > 0 else 0
        print(f"  Round {r+1}: {t:.4f}s, {bw:.2f} MB/s")

    # --- Benchmark 2: get_buffer with cache (1st round caches) ---
    print(f"\n--- get_buffer (cache=True) x {rounds} rounds ---")
    print("  (Round 1 triggers caching; subsequent rounds read from local)")
    for r in range(rounds):
        t = bench_get_buffer(store, keys, cache=True, label="cache")
        bw = total_mb / t if t > 0 else 0
        tag = " [caching]" if r == 0 else " [local]"
        print(f"  Round {r+1}{tag}: {t:.4f}s, {bw:.2f} MB/s")

    # --- Benchmark 3: get_into without cache ---
    print(f"\n--- get_into (cache=False) x {rounds} rounds ---")
    for r in range(rounds):
        t, _, _ = bench_get_into(
            store, keys, size_bytes, cache=False, label="no-cache")
        bw = total_mb / t if t > 0 else 0
        print(f"  Round {r+1}: {t:.4f}s, {bw:.2f} MB/s")

    # --- Benchmark 4: get_into with cache ---
    print(f"\n--- get_into (cache=True) x {rounds} rounds ---")
    print("  (Round 1 triggers caching; subsequent rounds read from local)")
    for r in range(rounds):
        t, _, _ = bench_get_into(
            store, keys, size_bytes, cache=True, label="cache")
        bw = total_mb / t if t > 0 else 0
        tag = " [caching]" if r == 0 else " [local]"
        print(f"  Round {r+1}{tag}: {t:.4f}s, {bw:.2f} MB/s")

    print("\nReader done.")


def main():
    parser = argparse.ArgumentParser(
        description="Cache-on-Get Benchmark Test")
    parser.add_argument(
        "--role", required=True, choices=["writer", "reader"],
        help="Role: 'writer' puts data, 'reader' benchmarks gets")
    parser.add_argument(
        "--num_keys", type=int, default=10,
        help="Number of keys to put/get (default: 10)")
    parser.add_argument(
        "--size_mb", type=float, default=64.0,
        help="Size of each value in MB (default: 64)")
    parser.add_argument(
        "--rounds", type=int, default=5,
        help="Number of benchmark rounds for reader (default: 5)")
    args = parser.parse_args()

    store = create_store()

    try:
        if args.role == "writer":
            run_writer(store, args)
        else:
            run_reader(store, args)
    finally:
        store.close()


if __name__ == "__main__":
    main()
