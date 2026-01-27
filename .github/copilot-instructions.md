# Mooncake Repository Guidelines for AI Assistants

## Repository Overview

Mooncake is a **KVCache-centric disaggregated architecture for LLM serving**, designed to serve large language models at scale with high efficiency. It is production-proven infrastructure powering [Kimi](https://kimi.ai/) by Moonshot AI.

### Key Technologies
- **Primary Languages**: C++ (core systems), CUDA (GPU acceleration), Python (bindings/integrations), CMake (build system), Shell (scripts)
- **Communication Protocols**: RDMA (InfiniBand/RoCEv2/eRDMA/NVIDIA GPUDirect), TCP, NVMe over Fabric (NVMe-of)
- **Core Components**: Transfer Engine, Mooncake Store, P2P Store, Elastic Expert Parallelism
- **Integrations**: vLLM, SGLang, LMDeploy, TensorRT-LLM, LMCache

### Project Structure
- `mooncake-transfer-engine/`: Core data transfer framework
- `mooncake-store/`: Distributed KVCache store
- `mooncake-p2p-store/`: Peer-to-peer object sharing
- `mooncake-integration/`: Integration with LLM inference systems
- `mooncake-ep/`: Elastic expert parallelism support
- `mooncake-common/`: Shared utilities and libraries
- `benchmarks/`: Performance evaluation tools
- `docs/`: Documentation and guides

## Coding Standards

### Style Guides
- **C++**: Follow [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- **Python**: Follow [Google Python Style Guide](https://google.github.io/styleguide/pyguide.html)
- **Documentation**: Well-documented code is essential for future contributors

### Pre-commit Hooks
The repository uses [pre-commit](https://pre-commit.com/) to enforce consistent formatting:
- **C/C++**: `clang-format` (see `.clang-format`)
- **Python**: `ruff` for linting and formatting
- **CMake**: `cmake-format`
- **Spelling**: `codespell`

**Setup**:
```bash
pip install -r requirements-dev.txt
pre-commit install
```

**Run checks**:
```bash
pre-commit run --all-files
```

## Build & Test

### Dependencies Installation
```bash
sudo bash -x dependencies.sh -y
```

### Build Commands
```bash
mkdir build
cd build
cmake .. -DUSE_HTTP=ON -DUSE_ETCD=ON -DSTORE_USE_ETCD=ON -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
sudo make install
```

### Testing
- **Unit Tests**: Located in respective component test directories
- **Integration Tests**: See `.github/workflows/integration-test.yml`
- **Coverage**: Use `lcov` and `gcovr` for coverage reports

### CI/CD
- **Main CI**: `.github/workflows/ci.yml` - Build & test on Ubuntu 22.04 with Python 3.10/3.12
- **Integration Tests**: `.github/workflows/integration-test.yml`
- **Release**: `.github/workflows/release.yaml` and `.github/workflows/release-non-cuda.yaml`

## Performance Considerations

### CUDA and GPU Operations
- **Memory Management**: Be mindful of VRAM allocations; use proper CUDA memory management APIs
- **Synchronization**: Ensure proper CUDA stream synchronization to avoid race conditions
- **Kernel Launches**: Verify kernel launch configurations for optimal performance
- **GPUDirect RDMA**: Understand RDMA-GPU memory interactions for zero-copy transfers

### RDMA and Networking
- **Bandwidth Aggregation**: Leverage multiple RDMA NICs when available
- **Topology Awareness**: Consider NUMA affinity in path selection
- **Error Handling**: Implement robust retry mechanisms for temporary network failures

### Concurrency
- **Thread Safety**: Ensure proper locking mechanisms in multi-threaded contexts
- **Lock-Free Data Structures**: Prefer lock-free designs where appropriate for high-throughput paths
- **Async Operations**: Use asynchronous I/O for non-blocking data transfers

## Documentation

When modifying user-facing behaviors:
- Update `docs/` with relevant guides and API documentation
- Follow existing documentation structure
- Include code examples where appropriate
- Update README.md if adding new features or components

## PR Classification

Use appropriate prefixes in PR titles:
- `[Bugfix]` - Bug fixes
- `[CI/Build]` - Build or continuous integration improvements
- `[Doc]` - Documentation fixes and improvements
- `[Integration]` - Changes in `mooncake-integration`
- `[P2PStore]` - Changes in `mooncake-p2p-store`
- `[Store]` - Changes in `mooncake-store`
- `[TransferEngine]` - Changes in `mooncake-transfer-engine`
- `[Misc]` - Other changes (use sparingly)

## RFC Process

For major architectural changes (>500 LOC excluding tests):
- Open a GitHub issue as an RFC (Request for Comments)
- Discuss technical design and justification
- Gather community feedback before implementation

## Community and Support

- **Documentation**: https://kvcache-ai.github.io/Mooncake/
- **Slack**: https://join.slack.com/t/mooncake-project/shared_invite/zt-3ig4fjai8-KH1zIm3x8Vm8WqyH0i_JaA
- **Issues**: GitHub Issues for bug reports and feature requests
- **Contributing Guide**: See `CONTRIBUTING.md` for detailed guidelines
