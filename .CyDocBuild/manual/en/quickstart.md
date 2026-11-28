---
id: quickstart
title: Start Chat Lab
description: Open the Windows example and send a local demonstration message before choosing an AI provider.
section: Start here
order: 1
revision: 5
parent: overview
previous: overview#workflows
next: chat-lab#connect
---

## Open Chat Lab {#build}

**Goal:** send a message to the local demonstration, then connect your own provider.
The demonstration needs no AI account, model or network connection.

For Windows 10/11 x64, extract the complete [published Chat Lab 0.5.6 ZIP](https://github.com/MrMybal/CyMCPSDK/releases/download/v0.5.6/CyMCPSDK-ChatLab-0.5.6-windows-x64.zip)
and run `cymcp_chat_app.exe`. That published example predates the current 0.7.5 source;
some controls described here require a source build.
Keep `Documentation/`, `configs/`, `licenses/`, `LICENSE` and `THIRD_PARTY_NOTICES.md`
beside the executable. Install the Microsoft Visual C++ 2015–2022 x64 runtime if needed.
The renderer uses DX11, with a WARP fallback.

To build the current Windows example, install Visual Studio 2022's **Desktop development
with C++** workload, Windows SDK and CMake tools, plus libcurl development files with TLS.
Obtain a compatible [CyUI checkout](https://github.com/MrMybal/CyUI) and pass its actual path:

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe
```

The script builds, tests and assembles `out/ChatLab-windows-x64`. Use `-CurlRoot PATH`
for a custom libcurl installation. For the Linux source build and SDL3 requirements,
see [build requirements](doc:integration#build).

## Try without an account {#demo}

Chat opens directly, in English. Automatic discovery may prepare an available provider
without sending a message. Choose the demonstration explicitly for this tutorial.

```steps
{"items":[{"title":"Select the local demonstration","body":"Open **Settings → Providers**, select **Demo / Markdown (no AI)** and choose **Connect / New chat**.","result":"Chat opens with the demonstration selected.","troubleshooting":"Return to Providers and select Demo explicitly if automatic discovery prepared another connection."},{"title":"Send a short message","body":"Type `Hello` and press **Enter**, or choose **Send**. **Ctrl+Enter** also sends; **Shift+Enter** inserts a line.","result":"A fixed local Markdown response appears progressively, including formatted text and code. No model is called.","troubleshooting":"Check that Demo is connected. For a real provider error, use the [connection checks](doc:troubleshooting#connection)."},{"title":"Read and copy the response","body":"Select text, or hover over a message and use **Copy message**. Use **Reply** to place a quote in the draft.","result":"The quote stays in the editable draft until you send it. **Stop** interrupts a response that is still running."}]}
```

```image
{"id":"chat-overview","src":"assets/chat-lab-en.png","alt":"Chat Lab in English showing its local Markdown demonstration","controls":"minimal"}
```

Next, [choose a real provider](doc:providers#configure) or explore the
[Chat Lab controls](doc:chat-lab#connect). The demonstration screenshot illustrates
the local response; provider settings are shown on the controls page.

## Language and help {#language}

In **Settings → Providers → Appearance**, choose **English** or **Français**, and a CyUI theme.
Reconnect the demonstration after changing language to produce new text in that language.
AI responses follow the selected model and your prompt; changing interface language
does not translate existing messages.

**F1** opens Chat Lab's Help panel. **Open full manual** opens this reader at
[Chat Lab](doc:chat-lab), in the selected language. Keep the complete local manual folder
beside the executable. Reading requires neither Node.js nor a web server.
