# Documentation toolchain

These files generate the human manual; they are not part of the native SDK runtime.

Use Node.js 22+ and run `npm ci`, `npm run build`, then `npm run check` from this
directory. Both generation commands write `../Documentation`. Author pages under
`manual/en` and `manual/fr`, with matching IDs, explicit anchors and current revision metadata.
The complete generated reader is distributed; `node_modules` and this workspace are not.

CyDoc 0.2.0 is supplied as the official local package built from
[`9837f815cdaa67e3206f68e15d04f4700f040dbf`](https://github.com/MrMybal/CyDoc/commit/9837f815cdaa67e3206f68e15d04f4700f040dbf).
The archive `vendor/cydoc-0.2.0.tgz` was produced with `npm ci` and `npm pack`
(which runs the upstream build through `prepack`) in a clean copy of that commit.
Its upstream reader, generator and package metadata are unchanged. It includes the
`ai/` guides and public declarations for future maintenance. Install this archive,
not the unrelated npm registry package. `package-lock.json` records versions and integrity.

Before another migration, read the installed `node_modules/cydoc/ai/README.md`,
`UPDATE.md`, `INTEGRATION.md`, `AUTHORING.md` and the `dist/*.d.ts` contracts.
The public manual contains product usage and SDK integration tasks. Keep assistant
instructions in the separately copied `DocAI.md` and maintenance records in `internal/`.
Page IDs, explicit anchors, media/node/port IDs and existing routes are public addresses.
`parent` controls the tree; `previous` and `next` independently define reading paths.
Review both translations before advancing `revision` / `sourceRevision` together.

| Component | Terms | Scope |
| --- | --- | --- |
| [CyDoc](https://github.com/MrMybal/CyDoc) 0.2.0 | [MIT](vendor/CyDoc-MIT.txt) | Generation and exported reader |
| [marked](https://github.com/markedjs/marked) | [MIT](licenses/marked-MIT.txt) | Build-time Markdown compilation |
| [PrismJS](https://github.com/PrismJS/prism) | [MIT](licenses/PrismJS-MIT.txt) | Build-time syntax highlighting |
| [esbuild](https://github.com/evanw/esbuild) | [MIT](licenses/esbuild-MIT.txt) | Bundling the static reader |

Only `image` and `graph` are enabled: real Chat Lab screenshots and the tool-discovery
graph. Static architecture and sequence diagrams compile to accessible SVG and need
no optional reader. No Three.js, remote fonts or CDN scripts enter the export.
There is no application logo in the project, so the reader keeps its default book icon.
The help button already builds an encoded local URL to `#/en/chat-lab` or `#/fr/chat-lab`;
F1 opens the in-app Help panel, whose full-manual button opens that URL.
Packaging already copies the complete generated `Documentation/` directory.

After generation, check the complete export through `file://` in a headless browser:
both languages, old routes/anchors, tree and reading paths, search, screenshot and
diagram enlargement, and graph text/exploration controls. Check wide and narrow layouts,
block network access, and inspect the actual output files and notices. Native F1 and
target-device rendering are separate checks; a headless reader check does not prove them.
Keep test/inspection notes under the ignored `internal/` directory or the ignored build directory.
