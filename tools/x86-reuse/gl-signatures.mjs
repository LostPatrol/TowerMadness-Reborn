// Derive Win32 OpenGL stack sizes from the existing official SDL headers.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// These headers describe the driver's stdcall ABI; pointers remain four bytes.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const headers = ['SDL_opengl.h', 'SDL_opengl_glext.h'];
const signatures = new Map();
for (const header of headers) {
  const source = fs.readFileSync(path.join(root, 'third_party/SDL3-3.4.18/include/SDL3', header), 'utf8');
  for (const match of source.matchAll(/GLAPI\s+[\w\s*]+?\s+(?:GLAPIENTRY|APIENTRY)\s+(gl\w+)\s*\(([^)]*)\)\s*;/g)) {
    const parameters = match[2].trim();
    const words = parameters === 'void' || !parameters ? 0 : parameters.split(',').reduce((count, parameter) =>
      count + (/\*/.test(parameter) ? 1 : /\b(GLdouble|GLclampd|GLuint64|GLint64|GLuint64EXT|GLint64EXT)\b/.test(parameter) ? 2 : 1), 0);
    signatures.set(match[1], words);
  }
}
fs.mkdirSync(path.join(root, 'agent/codex/x86-reuse'), { recursive: true });
fs.writeFileSync(path.join(root, 'agent/codex/x86-reuse/gl-signatures.json'), JSON.stringify(Object.fromEntries(signatures), null, 2));
console.log(`Recorded ${signatures.size} original driver signatures`);
