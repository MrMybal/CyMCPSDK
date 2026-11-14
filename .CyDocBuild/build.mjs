import { readFile, copyFile, writeFile, cp } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const workspace = dirname(fileURLToPath(import.meta.url));
const root = resolve(workspace, '..');
const args = process.argv.slice(2);
if (args.some(arg => arg !== '--check')) throw new Error('Usage: node build.mjs [--check]');
const config = JSON.parse(await readFile(resolve(workspace, 'cydoc.config.json'), 'utf8'));
const header = await readFile(resolve(root, 'include/cymcp/core.hpp'), 'utf8');
if (!header.includes(`version = "${config.version}"`)) throw new Error('Manual and SDK versions differ');
const command = args.includes('--check') ? 'check' : 'build';
const result = spawnSync(process.execPath,
  [resolve(workspace, 'node_modules/cydoc/tools/cli.mjs'), command, resolve(workspace, 'cydoc.config.json')],
  {cwd: workspace, stdio: 'inherit'});
if (result.error) throw result.error;
if (result.status !== 0) process.exit(result.status ?? 1);
// Product-specific agent guidance and notices are authored alongside the manual.
await copyFile(resolve(workspace, 'DocAI.md'), resolve(root, 'Documentation/DocAI.md'));
await copyFile(resolve(workspace, 'node_modules/cydoc/LICENSE'), resolve(root, 'Documentation/DocData/CyDoc-MIT.txt'));
await copyFile(resolve(root, 'LICENSE'), resolve(root, 'Documentation/DocData/CyMCPSDK-MIT.txt'));
await cp(resolve(workspace, 'licenses'), resolve(root, 'Documentation/DocData/notices'), {recursive: true});
await writeFile(resolve(root, 'Documentation/DocData/NOTICE.txt'),
  'CyMCPSDK documentation uses the CyDoc reader (MIT).\n' +
  'CyMCPSDK: Copyright (c) 2026 Cyberalien.\n' +
  'See CyMCPSDK-MIT.txt and CyDoc-MIT.txt in this directory.\n', 'utf8');
