import { readFile, copyFile, writeFile, cp } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const workspace = dirname(fileURLToPath(import.meta.url));
const root = resolve(workspace, '..');
const args = process.argv.slice(2);
if (args.some(arg => arg !== '--check')) throw new Error('Usage: node build.mjs [--check]');
const config = JSON.parse(await readFile(resolve(workspace, 'cydoc.config.json'), 'utf8'));
const cydoc = JSON.parse(await readFile(resolve(workspace, 'node_modules/cydoc/package.json'), 'utf8'));
if (cydoc.version !== '0.2.0') throw new Error('Install the pinned CyDoc 0.2.0 toolchain with npm ci');
const header = await readFile(resolve(root, 'include/cymcp/core.hpp'), 'utf8');
if (!header.includes(`version = "${config.version}"`)) throw new Error('Manual and SDK versions differ');
const command = args.includes('--check') ? 'check' : 'build';
const result = spawnSync(process.execPath,
  [resolve(workspace, 'node_modules/cydoc/tools/cli.mjs'), command, resolve(workspace, 'cydoc.config.json')],
  {cwd: workspace, stdio: 'inherit'});
if (result.error) throw result.error;
if (result.status !== 0) process.exit(result.status ?? 1);
// Adapt the upstream shell's metadata to this product; the viewer localizes its UI at mount.
const entry = resolve(root, 'Documentation/Documentation.html');
const html = await readFile(entry, 'utf8');
await writeFile(entry, html
  .replace('<html lang="fr">', '<html lang="en">')
  .replace('<title>CyDoc — Documentation</title>', '<title>CyMCPSDK — Documentation</title>')
  .replace('CyDoc — votre documentation, partout. Lecteur local et multilingue pour vos applications.',
    'CyMCPSDK: Chat Lab and C++ SDK integration. Complete English and French offline manual.')
  .replace(/\s*<link rel="icon"[^>]*>/, '') // No application favicon is supplied.
  .replace('Activez JavaScript pour utiliser le lecteur de documentation CyDoc.',
    'Enable JavaScript to read this manual. Activez JavaScript pour lire ce manuel.'), 'utf8');
// Product-specific agent guidance and notices are authored alongside the manual.
await copyFile(resolve(workspace, 'DocAI.md'), resolve(root, 'Documentation/DocAI.md'));
await copyFile(resolve(workspace, 'node_modules/cydoc/LICENSE'), resolve(root, 'Documentation/DocData/CyDoc-MIT.txt'));
await copyFile(resolve(root, 'LICENSE'), resolve(root, 'Documentation/DocData/CyMCPSDK-MIT.txt'));
await cp(resolve(workspace, 'licenses'), resolve(root, 'Documentation/DocData/notices'), {recursive: true});
await writeFile(resolve(root, 'Documentation/DocData/NOTICE.txt'),
  'CyMCPSDK documentation uses the CyDoc 0.2.0 reader (MIT).\n' +
  'CyMCPSDK: Copyright (c) 2026 Cyberalien.\n' +
  'See CyMCPSDK-MIT.txt and CyDoc-MIT.txt in this directory.\n', 'utf8');
