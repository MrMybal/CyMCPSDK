---
id: quickstart
title: Start Chat Lab
description: Build and open the Windows example, then try its local Markdown demonstration.
section: Guides
parent: overview
order: 1
revision: 2
---

## Build the example {#build}

Use Windows 10/11 x64 with Visual Studio 2022's **Desktop development with C++** workload,
Windows SDK and CMake tools. Obtain a compatible [CyUI checkout](https://github.com/MrMybal/CyUI)
beside the SDK, or pass its actual location to the script.

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe
```

The script compiles and assembles `out/ChatLab-windows-x64`. Keep its executable,
`Documentation`, `licenses`, `LICENSE` and `THIRD_PARTY_NOTICES.md` together.
The Microsoft Visual C++ 2015–2022 x64 runtime is required. DX11 is used, with a WARP fallback.

## Try without an account {#demo}

1. Start the application. **English** is the default language.
2. Disable **Automatic connection**, choose **Demo / Markdown (no AI)** and **Connect / New chat**.
3. Type a message and choose **Send**, or press **Ctrl+Enter**.
4. Read the streamed Markdown, select text or copy a message.

```image
{"id":"chat-overview","src":"assets/chat-lab-en.png","alt":"Chat Lab in English showing its local Markdown demonstration","controls":"minimal"}
```

The demonstration generates fixed local text; it does not call a model. Choose a
[provider](doc:providers) for an actual conversation. **Enter** adds a line in the composer.

## Language and help {#language}

Under **Appearance**, choose **English** or **Français**, and a CyUI theme.
Reconnect the demonstration after changing language to produce new text in that language.
AI responses follow the selected model and your prompt; changing interface language
does not translate existing messages.

**Help (F1)** opens this manual at [Chat Lab](doc:chat-lab). It uses the complete local
manual folder beside the executable. The reader does not need an HTTP server.
