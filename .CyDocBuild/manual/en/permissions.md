---
id: permissions
title: Configure permissions
description: Choose host operation policies and native agent approval behavior independently.
section: SDK
parent: integration
order: 2
revision: 1
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
**Allow all actions** requests the provider's unrestricted mode; **Allow counter changes**
controls the example operation registry. An enabled switch cannot remove restrictions imposed
by an external provider or operating system.

ACP filesystem and terminal client services are not announced by default. An embedding host
must declare `settings.client_capabilities`, provide `request_handler` and choose its approval
policy. The host implements the file/terminal behavior and cleanup; a capability flag alone
does not create a terminal service.

## Authenticate a local server {#network}

The loopback HTTP server requires a token of at least 32 characters and rejects requests carrying
`Origin`. Pass the token in `Authorization: Bearer ...`. Chat Lab generates a new random token
when its server starts; it displays only the endpoint and offers an explicit token-copy action.

Authentication identifies an allowed client; it does not replace operation authorization.
Keep the local server's lifetime, token distribution and domain policy under the host's control.
