# Documentation toolchain

These files generate the human manual; they are not part of the native SDK runtime.

Use Node.js 22+ and run `npm ci`, `npm run build`, then `npm run check` from this
directory. Both generation commands write `../Documentation`. Author pages under
`manual/en` and `manual/fr`, with matching IDs, explicit anchors and current revision metadata.
The complete generated reader is distributed; `node_modules` and this workspace are not.

CyDoc 0.1.0 is supplied as a generation-only package from the official repository at
commit `93f64486d61dae27a94ae1e5a05a20f6edd99914`. It retains `dist` entry points/modules,
`tools`, `web`, `src/viewer.css` and `LICENSE`; example manuals, screenshots and authoring
guides are omitted. Reader/generator code is unchanged. Package metadata lists only
these files and omits unused upstream development scripts. The local archive avoids
confusing it with an unrelated npm package. `package-lock.json` records all resolved
versions and integrity values.

| Component | Terms | Scope |
| --- | --- | --- |
| [CyDoc](https://github.com/MrMybal/CyDoc) 0.1.0 | [MIT](vendor/CyDoc-MIT.txt) | Generation and exported reader |
| [marked](https://github.com/markedjs/marked) | [MIT](licenses/marked-MIT.txt) | Build-time Markdown compilation |
| [PrismJS](https://github.com/PrismJS/prism) | [MIT](licenses/PrismJS-MIT.txt) | Build-time syntax highlighting |
| [esbuild](https://github.com/evanw/esbuild) | [MIT](licenses/esbuild-MIT.txt) | Bundling the static reader |

No optional mesh, video or other media modules are enabled. The image module handles
the actual Chat Lab screenshots. There are no remote fonts or CDN scripts in the reader.
Keep test/inspection notes under the ignored `internal/` directory or the ignored build directory.
