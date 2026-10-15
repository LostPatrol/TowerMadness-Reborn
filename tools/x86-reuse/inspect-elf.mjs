// Inspect the supplied little-endian ELF32 library without executing game code.
import fs from 'node:fs';
import crypto from 'node:crypto';

// Both original ABIs use little-endian ELF32; reject other layouts explicitly.
const input = process.argv[2];
const bytes = fs.readFileSync(input);
if (bytes.subarray(0, 4).toString('hex') !== '7f454c46' || bytes[4] !== 1 || bytes[5] !== 1) {
  throw new Error('Expected little-endian ELF32');
}
// ELF32 section and symbol layouts are fixed by the ELF specification.
const sectionOffset = bytes.readUInt32LE(32);
const sectionSize = bytes.readUInt16LE(46);
const sectionCount = bytes.readUInt16LE(48);
const sectionNameIndex = bytes.readUInt16LE(50);
const symbolSize = 16;
const sections = [];

function cstring(offset) {
  // Decode a NUL-terminated ASCII name from the original file.
  const end = bytes.indexOf(0, offset);
  return bytes.toString('utf8', offset, end < 0 ? bytes.length : end);
}

for (let index = 0; index < sectionCount; index++) {
  const at = sectionOffset + index * sectionSize;
  sections.push({ nameOffset: bytes.readUInt32LE(at), type: bytes.readUInt32LE(at + 4),
    address: bytes.readUInt32LE(at + 12), offset: bytes.readUInt32LE(at + 16),
    size: bytes.readUInt32LE(at + 20), link: bytes.readUInt32LE(at + 24),
    entrySize: bytes.readUInt32LE(at + 36) });
}
for (const section of sections) section.name = cstring(sections[sectionNameIndex].offset + section.nameOffset);
const symbols = [];
for (const section of sections.filter(section => [2, 11].includes(section.type))) {
  const strings = sections[section.link];
  for (let at = section.offset; at < section.offset + section.size; at += section.entrySize || symbolSize) {
    const name = cstring(strings.offset + bytes.readUInt32LE(at));
    if (!name) continue;
    symbols.push({ table: section.name, name, address: bytes.readUInt32LE(at + 4),
      size: bytes.readUInt32LE(at + 8), type: bytes[at + 12] & 15,
      binding: bytes[at + 12] >> 4, sectionIndex: bytes.readUInt16LE(at + 14) });
  }
}
const dependencies = [];
for (const section of sections.filter(section => section.type === 6)) {
  const strings = sections[section.link];
  for (let at = section.offset; at < section.offset + section.size; at += section.entrySize || 8) {
    if (bytes.readInt32LE(at) === 1) dependencies.push(cstring(strings.offset + bytes.readUInt32LE(at + 4)));
  }
}
// Function exports identify candidates; a name alone does not recover semantics.
const exportedFunctions = symbols.filter(symbol => symbol.table === '.dynsym' && symbol.type === 2 && symbol.sectionIndex !== 0);
const gameFunctions = exportedFunctions.filter(symbol => /^(GAME|ENEMIES|TOWERS|WAVES|PATH|MAP|WEAPONS|REPLAY)_/.test(symbol.name));
console.log(JSON.stringify({ input, sha256: crypto.createHash('sha256').update(bytes).digest('hex'),
  machine: bytes.readUInt16LE(18), elfClass: 32, littleEndian: true, dependencies,
  sections, exportedFunctionCount: exportedFunctions.length, gameFunctionCount: gameFunctions.length,
  gameFunctions, symbols }, null, 2));
