---
id: licenses
title: Licenses and components
description: Understand the MIT SDK license and separately licensed optional components.
section: Help
order: 31
revision: 4
previous: troubleshooting#help
next:
---

## CyMCPSDK {#sdk}

Copyright © 2026 Cyberalien. CyMCPSDK source is distributed under the MIT license.
Keep the copyright and permission notice with redistributed copies. The source root's
`LICENSE` and `THIRD_PARTY_NOTICES.md` contain the terms and component notices.

## Components {#components}

| Component | License | Scope |
| --- | --- | --- |
| nlohmann/json 3.12.0 | MIT | SDK JSON representation and validation |
| cpp-httplib 0.66.0 | MIT | Local HTTP server and test fixtures |
| PicoSHA2 | MIT | Artifact verification |
| Host libcurl | curl license and host TLS terms | HTTP(S) on Windows/Linux/macOS |
| CyUI, Dear ImGui, MD4C | MIT | Optional native chat rendering |
| SDL3 | zlib | Linux windowed example only |
| stb image/image_write | MIT option | Linux example image decoding and PNG capture |
| Lexilla/Scintilla lexers | Their permissive notices | Optional code highlighting through CyUI |
| JetBrains Mono | SIL Open Font License 1.1 | CyUI's embedded font |
| CyDoc 0.2.0 reader | MIT | This static documentation reader, including image and graph modules |
| marked, PrismJS, esbuild | MIT | Documentation generation tools only |

The application packages carry the complete optional UI notices in `licenses/`.
This manual includes the SDK and CyDoc MIT texts in `DocData/`.
No inference engine, model or GPU runtime acquires the SDK's MIT license: separately
installed components and provider services retain their own terms.
