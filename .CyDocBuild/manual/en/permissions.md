---
id: permissions
title: Configure permissions
description: Choose host operation policies and native agent approval behavior independently.
section: SDK
parent: integration
order: 2
revision: 2
---

## Choose the host policy {#policy}

A default `Policy` denies operations. Use a callback to decide from the call context,
operation name and arguments, or explicitly use `Policy::allow_all()` when the host wants
unrestricted access to its registered domain. The SDK does not require confirmation dialogs.

```cpp
cymcp::Policy reads_only([](const auto&, const std::string& name, const auto&) {
    return name == "get_counter" ? cymcp::Decision::allow : cymcp::Decision::deny;
});
```

Permission to call one operation does not create new application capabilities. Keep operation
handlers and schemas as narrow as the application needs. Domain validation still applies in an
allow-all policy. Cancellation and resource budgets are separate from permission decisions.

## Agent approval and app operations {#agents}

Native agent permissions are separate from the operation registry. In Chat Lab,
**Allow all actions** requests the provider's unrestricted mode. The **MCP access profile**
controls permissions on the example operation registry. The provider and operating system apply
their own permissions.

ACP filesystem and terminal client services are not announced by default. An embedding host
must declare `settings.client_capabilities`, provide `request_handler` and choose its approval
policy. The host implements the file/terminal behavior and cleanup; a capability flag alone
does not create a terminal service.

## Authenticate a local server {#network}

The token-protected loopback server accepts a strong token of at least 32 characters in
`Authorization: Bearer ...` or a credential validated by the host's authentication callback.
For explicit local access without credentials, use `LocalHttpServer::UnauthenticatedLoopback`.
The server binds to `127.0.0.1` and rejects requests carrying `Origin` in both modes.

`local_access_settings(LocalAccessProfile)` provides presets for the token requirement and the
host's operation policy. Choose the transport and registry policy separately from those settings.
See the [Chat Lab profiles](doc:chat-lab#external) for the four combinations.
