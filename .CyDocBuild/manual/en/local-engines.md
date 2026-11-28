---
id: local-engines
title: Local engines and dependencies
description: Connect an existing service or supervise an installed llama.cpp-compatible engine.
section: Develop with the SDK
order: 23
revision: 3
parent: integration
previous: permissions#policy
next: optimization#tools
---

## Use an existing service {#service}

Choose `ollama` for Ollama's native API, `lmstudio` for its compatible server, or `llama.cpp`
for an already running compatible endpoint. Set the actual service URL and a model it provides.
Choosing a provider profile does not install or launch that product.

Chat Lab connects to installed local services. Install the service and model separately. On Windows
x64 it also offers the optional [CUDA runtime download](doc:local-engines#dependencies); that
download supplies neither a model nor a GPU driver.

## Supervise a native engine {#supervise}

`LocalEngine` owns a llama.cpp-compatible child process. Its options include the native executable,
model path, private working directory, context token count, GPU layers and startup timeout.
The executable and model must already exist and be compatible with each other.

```cpp
#include <cymcp/local.hpp>

void start_local(const std::filesystem::path& server_executable,
                 const std::filesystem::path& model,
                 const std::filesystem::path& private_work_directory) {
    cymcp::LocalEngineOptions options;
    options.process.executable = server_executable;
    options.model = model;
    options.private_work_directory = private_work_directory;
    cymcp::LocalEngine engine(options);
    engine.start();
    // While engine remains alive, configure an API provider with
    // engine.base_url() and engine.token(), then run the host's conversation.
    engine.stop();
}
```

Keep the engine alive until conversations finish. The supervisor checks readiness, supplies a private
authentication file and stops its owned process tree. It does not manage an unrelated user service.
Choose GPU settings according to the installed engine and runtime; no GPU component is bundled.

## Download verified components {#dependencies}

`DependencyManager` consumes a trusted host-supplied `Package` manifest containing artifact URLs,
relative paths, byte sizes, SHA-256 hashes and optional installer arguments. It provides `install`,
`verify`, `run_installer` and `uninstall`. Its host policy decides whether those actions are allowed;
an elevation callback handles installers that need it.

Keep manifests under application control, never model output. Artifact paths stay in the configured
cache; HTTPS and declared redirect hosts constrain downloads. External packages retain their own
licenses. A host may declare individual regular archive members with their destination, size and
SHA-256. The system tar reader extracts them into a private staging area before publication.

On Windows x64, select a local service in Chat Lab's **Providers** tab, open **Installation and
connection** and choose **Install NVIDIA CUDA 12 runtime**. Chat Lab downloads NVIDIA CUDART
12.8.90 and its license to the user's private dependency cache. It shows the runtime directory
when verification succeeds. A compatible NVIDIA driver and any other libraries required by the
chosen engine are separate prerequisites.

Windows and Linux x64 hosts can run the same installation on a worker and supply the directory
to an owned engine. The supervisor applies the library search path to that child process.

```cpp
cymcp::DependencyManager dependencies({private_cache, host_policy});
auto installed = dependencies.install(cymcp::cuda_runtime_package(), context);
options.cuda_runtime_directory = installed / "runtime"; // LocalEngineOptions
```

The runtime is pinned to NVIDIA's [CUDA 12.8.1 redistribution manifest](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.8.1.json).
The installed package includes NVIDIA's license. HTTP Range resume and automatic GPU selection remain future work.
