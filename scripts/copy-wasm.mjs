import { cpSync, existsSync, mkdirSync, readdirSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const config = 'sapp-gles-emsc-ninja-release';
const src = resolve(root, 'native/.fibs/dist', config);
const dst = resolve(root, 'web/public/wasm');

if (!existsSync(resolve(src, 'main.js'))) {
  console.error(`wasm build not found in ${src}`);
  console.error('run "npm run build:wasm" first');
  process.exit(1);
}

rmSync(dst, { recursive: true, force: true });
mkdirSync(dst, { recursive: true });

const files = readdirSync(src).filter((f) => f.startsWith('main.') && !f.endsWith('.html'));
for (const file of files) {
  cpSync(resolve(src, file), resolve(dst, file));
}

console.log(`copied ${files.join(', ')} -> web/public/wasm`);
