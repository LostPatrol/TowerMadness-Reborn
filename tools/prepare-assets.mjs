// Validate and normalize the original APK resources without executing game code.
// Outputs retain source names and values; unresolved dependencies remain explicit.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';

// The APK VBO header uses three packed 13-byte attribute descriptors.
const VBO_HEADER_SIZE = 63;
// Normalized meshes use a 16-byte header and five float32 values per vertex.
const MESH_HEADER_SIZE = 16;
// These resource types are JSON-like text, with optional trailing commas.
const TEXT_TYPES = new Set(['.gamemode', '.map', '.waves', '.tech', '.model', '.material', '.texture', '.atlas', '.sound']);
// OpenGL constants identify the original vertex and primitive layouts.
const GL = { float: 5126, byte: 5121, short: 5123, triangles: 4, strip: 5 };
// Default world resources are selected by native code rather than map fields.
const WORLD_ROOTS = ['basetiles.atlas', 'transitions.atlas', 'sheep.model', 'ufo.model', 'beam.model', 'skybox.model', 'turretbase.model', 'turretbase_1.model', 'turretbase_2.model', 'turretbase_3.model', 'turretbase_4.model', 'turretbase_5.model', 'turretbase_max.model'];
// Share the canonical original assets with the x86 host instead of keeping a second APK extraction.
const defaultSourceRoot = path.resolve('agent/codex/x86-reuse/package/assets');
// An explicit caller path remains supported; only the canonical extraction has known APK provenance.
const sourceRoot = path.resolve(process.argv[2] ?? defaultSourceRoot);
// Hash the actual canonical APK rather than carrying the retired ARM package identity into new output.
const sourceApk = sourceRoot === defaultSourceRoot ? path.resolve('Madness_Zero-1.22.apk') : null;
const outputRoot = path.resolve(process.argv[3] ?? 'assets');

/** Enumerate only the supplied asset directory in stable filename order. */
function enumerate(directory, prefix = '') {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const name = prefix + entry.name;
    return entry.isDirectory() ? enumerate(path.join(directory, entry.name), name + '/') : [name];
  }).sort();
}

/** Hash the original bytes for reproducible resource provenance. */
function sha256(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

/** Write an output file after creating its parent directory. */
function write(relative, value) {
  const destination = path.join(outputRoot, relative);
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.writeFileSync(destination, value);
}

/** Remove commas before closing delimiters without changing quoted strings. */
function removeTrailingCommas(text) {
  let result = '', quoted = false, escaped = false;
  for (let i = 0; i < text.length; i++) {
    const character = text[i];
    if (!quoted && character === ',' && /^\s*[}\]]/.test(text.slice(i + 1))) continue;
    result += character;
    if (escaped) escaped = false;
    else if (quoted && character === '\\') escaped = true;
    else if (character === '"') quoted = !quoted;
  }
  return result;
}

/** Parse known text defects and record each deliberate normalization. */
function parseText(name, bytes) {
  const original = bytes.toString('utf8').replace(/^\uFEFF/, '');
  let normalized = removeTrailingCommas(original);
  const repairs = normalized === original ? [] : ['trailing commas removed'];
  // The shipped font atlas closes its root without closing subtextures.
  if (name === 'font.atlas') {
    normalized = normalized.replace(/}\s*}\s*$/, '}\n]\n}');
    repairs.push('missing subtextures closing bracket inserted');
  }
  return { value: JSON.parse(normalized), repairs };
}

/** Convert a validated APK VBO into float32 XYZ/UV and triangle uint16 indices. */
function convertVbo(name, bytes) {
  if (bytes.length < VBO_HEADER_SIZE + 24) throw new Error('VBO is shorter than its header');
  const attributes = [4, 17, 30].map(at => ({ components: bytes[at], type: bytes.readUInt32LE(at + 1), offset: bytes.readUInt32LE(at + 5), stride: bytes.readUInt32LE(at + 9) }));
  const vertexBytes = bytes.readUInt32LE(43), primitive = bytes.readUInt32LE(47);
  const indexType = bytes.readUInt32LE(51), indexBytes = bytes.readUInt32LE(55);
  const indexOffset = VBO_HEADER_SIZE + vertexBytes;
  if (bytes.readUInt32LE(0) !== 4 || bytes.readUInt32LE(59) !== 0 || bytes.length !== indexOffset + indexBytes + 24) throw new Error('Unknown VBO header or payload length');
  const position = attributes[0], uv = attributes[1];
  if (![2, 3].includes(position.components) || uv.components !== 2 || attributes[2].components !== 0 || position.type !== GL.float || uv.type !== GL.float || position.stride !== uv.stride || vertexBytes % position.stride !== 0) throw new Error('Unsupported vertex attribute layout');
  if (position.offset + position.components * 4 > position.stride || uv.offset + 8 > uv.stride) throw new Error('Attribute outside vertex stride');
  const vertexCount = vertexBytes / position.stride;
  const vertices = Buffer.alloc(vertexCount * 20);
  const bounds = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  for (let vertex = 0; vertex < vertexCount; vertex++) {
    const at = VBO_HEADER_SIZE + vertex * position.stride;
    const values = [0, 1, 2].map(axis => axis < position.components ? bytes.readFloatLE(at + position.offset + axis * 4) : 0);
    values.push(bytes.readFloatLE(at + uv.offset), bytes.readFloatLE(at + uv.offset + 4));
    if (!values.every(Number.isFinite)) throw new Error(`Nonfinite vertex ${vertex}`);
    values.forEach((value, component) => vertices.writeFloatLE(value, vertex * 20 + component * 4));
    for (let axis = 0; axis < 3; axis++) {
      bounds.min[axis] = Math.min(bounds.min[axis], values[axis]);
      bounds.max[axis] = Math.max(bounds.max[axis], values[axis]);
    }
  }
  const indexWidth = indexType === GL.short ? 2 : indexType === GL.byte ? 1 : 0;
  if (!indexWidth || indexBytes % indexWidth !== 0) throw new Error('Unsupported index type');
  const originalIndices = Array.from({ length: indexBytes / indexWidth }, (_, i) => indexWidth === 2 ? bytes.readUInt16LE(indexOffset + i * 2) : bytes[indexOffset + i]);
  if (originalIndices.some(index => index >= vertexCount)) throw new Error('Index outside vertex buffer');
  let indices = originalIndices;
  if (primitive === GL.strip) {
    indices = [];
    for (let i = 2; i < originalIndices.length; i++) {
      const triangle = i % 2 ? [originalIndices[i - 1], originalIndices[i - 2], originalIndices[i]] : originalIndices.slice(i - 2, i + 1);
      if (new Set(triangle).size === 3) indices.push(...triangle);
    }
  } else if (primitive !== GL.triangles || indices.length % 3 !== 0) throw new Error('Unsupported primitive layout');
  const header = Buffer.alloc(MESH_HEADER_SIZE);
  header.write('TMMS', 0, 'ascii');
  header.writeUInt32LE(1, 4);
  header.writeUInt32LE(vertexCount, 8);
  header.writeUInt32LE(indices.length, 12);
  const indexBuffer = Buffer.alloc(indices.length * 2);
  indices.forEach((index, i) => indexBuffer.writeUInt16LE(index, i * 2));
  const output = `meshes/${name.replace(/\.vbo$/, '.mesh')}`;
  write(output, Buffer.concat([header, vertices, indexBuffer]));
  return { output, vertexCount, indexCount: indices.length, triangleCount: indices.length / 3, originalPrimitive: primitive, originalIndexType: indexType, attributes, bounds, trailerHex: bytes.subarray(-24).toString('hex') };
}

/** Compute PNG's standard CRC32 over a chunk type and its payload. */
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const value of bytes) {
    crc ^= value;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/** Check PNG chunk CRCs and compressed pixels without displaying the image. */
function inspectPng(bytes) {
  if (bytes.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a') throw new Error('Invalid PNG signature');
  let at = 8, dimensions, ended = false;
  const data = [];
  while (at < bytes.length) {
    if (at + 12 > bytes.length) throw new Error('Truncated PNG chunk');
    const length = bytes.readUInt32BE(at), type = bytes.toString('ascii', at + 4, at + 8);
    if (at + length + 12 > bytes.length) throw new Error('PNG chunk outside file');
    if (type === 'CgBI') throw new Error('Apple CgBI PNG uses nonstandard raw-deflate/BGRA pixels; excluded from standard PNG conversion');
    if (crc32(bytes.subarray(at + 4, at + 8 + length)) !== bytes.readUInt32BE(at + 8 + length)) throw new Error(`PNG CRC mismatch in ${type}`);
    if (type === 'IHDR') dimensions = { width: bytes.readUInt32BE(at + 8), height: bytes.readUInt32BE(at + 12), bitDepth: bytes[at + 16], colorType: bytes[at + 17] };
    if (type === 'IDAT') data.push(bytes.subarray(at + 8, at + 8 + length));
    at += length + 12;
    if (type === 'IEND') { ended = true; break; }
  }
  if (!dimensions || !ended || at !== bytes.length) throw new Error('Incomplete PNG structure');
  const inflatedBytes = zlib.inflateSync(Buffer.concat(data)).length;
  return { ...dimensions, inflatedBytes, validation: 'signature, chunks, CRC and IDAT decompression; renderer checks appearance' };
}

/** Validate Ogg page framing and identify the Vorbis identification packet silently. */
function inspectOgg(bytes) {
  let at = 0, pages = 0;
  while (at < bytes.length) {
    if (bytes.toString('ascii', at, at + 4) !== 'OggS' || at + 27 > bytes.length || bytes[at + 4] !== 0) throw new Error('Invalid Ogg page');
    const segments = bytes[at + 26];
    if (at + 27 + segments > bytes.length) throw new Error('Truncated Ogg segment table');
    const payload = [...bytes.subarray(at + 27, at + 27 + segments)].reduce((total, size) => total + size, 0);
    at += 27 + segments + payload;
    if (at > bytes.length) throw new Error('Truncated Ogg payload');
    pages++;
  }
  const identification = bytes.indexOf(Buffer.from('\x01vorbis', 'binary'));
  if (identification < 0 || identification + 16 > bytes.length) throw new Error('No Vorbis identification packet');
  return { codec: 'vorbis', pages, channels: bytes[identification + 11], sampleRate: bytes.readUInt32LE(identification + 12), validation: 'page framing and identification; no playback or audio decode' };
}

/** Resolve resource references by their actual file or material/sound dictionary. */
function collectReferences(name, value, materialIndex, soundIndex) {
  const references = [];
  const add = (kind, target, field) => {
    if (typeof target !== 'string' || !target) return;
    let candidates;
    if (kind === 'material') candidates = materialIndex.get(target) ?? [];
    else if (kind === 'sound') candidates = soundIndex.get(target) ?? [];
    else candidates = [`${target}.${kind}`];
    references.push({ field, kind, target, candidates });
  };
  const ext = path.extname(name);
  if (ext === '.gamemode') for (const key of ['map', 'waves', 'tech', 'logoatlas']) add(key === 'logoatlas' ? 'atlas' : key, value[key], key);
  if (ext === '.map') {
    for (const key of ['transitionAtlas', 'basetileAtlas']) add('atlas', value[key], key);
    for (const [i, model] of (value.treeModels ?? []).entries()) add('model', model, `treeModels[${i}]`);
    for (const [i, doodad] of (value.doodads ?? []).entries()) add('model', doodad.model, `doodads[${i}].model`);
  }
  if (ext === '.model') for (const [key, geoset] of Object.entries(value.geosets ?? {})) {
    add('vbo', geoset.vbo, `geosets.${key}.vbo`);
    add('material', geoset.material, `geosets.${key}.material`);
  }
  if (ext === '.atlas') add('material', value.material, 'material');
  if (ext === '.material') for (const [key, material] of Object.entries(value)) add('texture', material.tex, `${key}.tex`);
  if (ext === '.texture') {
    const suffix = value.type === 'pvr' ? '.pvr.png' : '.png';
    references.push({ field: 'file', kind: 'image', target: value.file, candidates: [`${value.file}${suffix}`] });
  }
  if (ext === '.sound') for (const [key, sound] of Object.entries(value)) if (sound.file) add('caf', sound.file, `${key}.file`);
  if (ext === '.waves') for (const [i, wave] of (Array.isArray(value) ? value : value.types ?? []).entries()) {
    for (const key of ['model', 'submodel']) add('model', wave[key], `[${i}].${key}`);
    for (const key of ['walk', 'subwalk']) for (const model of wave[key] ?? []) add('model', model, `[${i}].${key}`);
    for (const sound of wave.deathsounds ?? []) add('sound', sound, `[${i}].deathsounds`);
  }
  if (ext === '.tech') {
    for (const [key, tower] of Object.entries(value.towers ?? {})) for (const field of ['model', 'turret']) add('model', tower[field], `towers.${key}.${field}`);
    for (const [key, weapon] of Object.entries(value.weapons ?? {})) for (const field of ['firesounds', 'hitsounds']) for (const sound of weapon[field] ?? []) add('sound', sound, `weapons.${key}.${field}`);
  }
  return references;
}

/** Prepare all assets and report unresolved dependency edges instead of hiding them. */
function main() {
  if (!fs.existsSync(sourceRoot)) throw new Error(`Missing extracted APK assets: ${sourceRoot}`);
  const records = [], parsed = new Map(), anomalies = [], materials = new Map(), sounds = new Map();
  for (const name of enumerate(sourceRoot)) {
    const bytes = fs.readFileSync(path.join(sourceRoot, name)), extension = path.extname(name).toLowerCase();
    const record = { name, bytes: bytes.length, sha256: sha256(bytes), type: extension.slice(1) || 'other' };
    try {
      if (TEXT_TYPES.has(extension)) {
        const result = parseText(name, bytes);
        parsed.set(name, result.value);
        record.output = `data/${name}.json`;
        record.repairs = result.repairs;
        write(record.output, JSON.stringify(result.value, null, 2) + '\n');
        if (extension === '.material' || extension === '.sound') {
          const index = extension === '.material' ? materials : sounds;
          for (const key of Object.keys(result.value)) index.set(key, [...(index.get(key) ?? []), name]);
        }
      } else if (extension === '.vbo') record.mesh = convertVbo(name, bytes);
      else if (extension === '.png') {
        record.image = inspectPng(bytes);
        record.output = `textures/${name}`;
        write(record.output, bytes);
      } else if (extension === '.caf') {
        record.audio = inspectOgg(bytes);
        record.output = `audio/${name.replace(/\.caf$/, '.ogg')}`;
        write(record.output, bytes);
      }
      record.status = TEXT_TYPES.has(extension) || ['.vbo', '.png', '.caf'].includes(extension) ? 'ok' : 'inventory-only';
    } catch (error) {
      record.status = 'error';
      record.error = error.message;
      anomalies.push({ name, error: error.message });
    }
    records.push(record);
  }
  const byName = new Map(records.map(record => [record.name, record]));
  for (const [name, value] of parsed) byName.get(name).references = collectReferences(name, value, materials, sounds);
  const levels = [];
  for (const name of [...parsed.keys()].filter(name => name.endsWith('.gamemode')).sort()) {
    const visited = new Set(), missing = [], queue = [name, ...WORLD_ROOTS];
    while (queue.length) {
      const node = queue.shift();
      if (visited.has(node)) continue;
      visited.add(node);
      const [target, selectedKey] = node.split('#');
      const record = byName.get(target);
      if (!record || record.status !== 'ok') { missing.push({ source: name, target, reason: record?.error ?? 'missing file' }); continue; }
      for (const reference of record.references ?? []) {
        // Containers hold unused historical entries; follow only the requested key.
        if (selectedKey && reference.field !== `${selectedKey}.tex` && reference.field !== `${selectedKey}.file`) continue;
        const found = reference.candidates.filter(candidate => byName.has(candidate));
        if (!found.length) missing.push({ source: target, field: reference.field, kind: reference.kind, target: reference.target, reason: 'unresolved reference' });
        else queue.push(...found.map(file => ['material', 'sound'].includes(reference.kind) ? `${file}#${reference.target}` : file));
      }
    }
    const uniqueMissing = [...new Map(missing.map(item => [JSON.stringify(item), item])).values()];
    levels.push({ name, resources: [...new Set([...visited].map(node => node.split('#')[0]))].sort(), nodes: [...visited].sort(), missing: uniqueMissing, closureComplete: uniqueMissing.length === 0 });
  }
  const counts = records.reduce((total, record) => ({ ...total, [record.type]: (total[record.type] ?? 0) + 1 }), {});
  const unresolvedReferences = records.flatMap(record => (record.references ?? []).filter(reference => !reference.candidates.some(candidate => byName.has(candidate))).map(reference => ({ source: record.name, ...reference })));
  const manifest = { formatVersion: 1, source: { directory: path.relative(process.cwd(), sourceRoot).replaceAll('\\', '/'), apkSha256: sourceApk && fs.existsSync(sourceApk) ? sha256(fs.readFileSync(sourceApk)) : null }, meshFormat: { magic: 'TMMS', version: 1, endian: 'little', headerBytes: 16, vertex: 'float32 x,y,z,u,v', index: 'uint16 triangles' }, dependencyCoverage: 'Explicit JSON references and selected known default world models; arbitrary native-code dynamic references are not proven complete.', counts, anomalies, unresolvedReferences, records, levels };
  write('manifest.json', JSON.stringify(manifest, null, 2) + '\n');
  const sample = { gamemode: parsed.get('PlainPlane.gamemode'), map: parsed.get('PlainPlane.map'), waves: parsed.get('PlainPlane.waves'), tech: parsed.get('Easy.tech'), model: parsed.get('potatogun_i.model'), firstEnemyModel: parsed.get('grey.model'), level: levels.find(level => level.name === 'PlainPlane.gamemode') };
  write('samples/plain-plane.json', JSON.stringify(sample, null, 2) + '\n');
  console.log(JSON.stringify({ counts, errors: anomalies, levels: levels.length, completeLevels: levels.filter(level => level.closureComplete).length, plainPlaneMissing: sample.level?.missing, output: outputRoot }, null, 2));
}

main();
