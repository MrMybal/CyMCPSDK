---
id: local-engines
title: Local engines and dependencies
description: Connect an existing service or supervise an installed llama.cpp-compatible engine.
section: SDK
parent: integration
order: 3
revision: 1
---

## Use an existing service {#service}

Choose `ollama` for Ollama's native API, `lmstudio` for its compatible server, or `llama.cpp`
for an already running compatible endpoint. Set the actual service URL and a model it provides.
Choosing a provider profile does not install or launch that product.

Chat Lab connects to local services. It does not currently offer a model or GPU package installer.
The application embedding the SDK can provide its own installation workflow.

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
licenses. Automatic archive extraction, HTTP Range resumption and GPU package catalogues remain
outside the current implementation.
