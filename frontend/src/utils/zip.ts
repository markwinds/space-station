export interface ZipFile {
  filename: string;
  content: string;
}

interface EncodedZipFile {
  filename: Uint8Array;
  content: Uint8Array;
  crc32: number;
  offset: number;
}

const textEncoder = new TextEncoder();
const crcTable = buildCrcTable();

export function createZipBlob(files: ZipFile[]): Blob {
  const encodedFiles: EncodedZipFile[] = files.map((file) => ({
    filename: textEncoder.encode(file.filename),
    content: textEncoder.encode(file.content),
    crc32: crc32(textEncoder.encode(file.content)),
    offset: 0,
  }));

  const localParts: Uint8Array[] = [];
  let offset = 0;
  for (const file of encodedFiles) {
    file.offset = offset;
    const header = createLocalFileHeader(file);
    localParts.push(header, file.content);
    offset += header.byteLength + file.content.byteLength;
  }

  const centralDirectoryOffset = offset;
  const centralParts = encodedFiles.map(createCentralDirectoryHeader);
  const centralDirectorySize = centralParts.reduce((size, part) => size + part.byteLength, 0);
  const endRecord = createEndOfCentralDirectoryRecord(encodedFiles.length, centralDirectorySize, centralDirectoryOffset);
  const zipBytes = concatBytes([...localParts, ...centralParts, endRecord]);

  return new Blob([toArrayBuffer(zipBytes)], { type: "application/zip" });
}

function createLocalFileHeader(file: EncodedZipFile): Uint8Array {
  const header = new Uint8Array(30 + file.filename.byteLength);
  const view = new DataView(header.buffer);
  writeUint32(view, 0, 0x04034b50);
  writeUint16(view, 4, 20);
  writeUint16(view, 6, 0x0800);
  writeUint16(view, 8, 0);
  writeDosTimestamp(view, 10);
  writeUint32(view, 14, file.crc32);
  writeUint32(view, 18, file.content.byteLength);
  writeUint32(view, 22, file.content.byteLength);
  writeUint16(view, 26, file.filename.byteLength);
  writeUint16(view, 28, 0);
  header.set(file.filename, 30);
  return header;
}

function createCentralDirectoryHeader(file: EncodedZipFile): Uint8Array {
  const header = new Uint8Array(46 + file.filename.byteLength);
  const view = new DataView(header.buffer);
  writeUint32(view, 0, 0x02014b50);
  writeUint16(view, 4, 20);
  writeUint16(view, 6, 20);
  writeUint16(view, 8, 0x0800);
  writeUint16(view, 10, 0);
  writeDosTimestamp(view, 12);
  writeUint32(view, 16, file.crc32);
  writeUint32(view, 20, file.content.byteLength);
  writeUint32(view, 24, file.content.byteLength);
  writeUint16(view, 28, file.filename.byteLength);
  writeUint16(view, 30, 0);
  writeUint16(view, 32, 0);
  writeUint16(view, 34, 0);
  writeUint16(view, 36, 0);
  writeUint32(view, 38, 0);
  writeUint32(view, 42, file.offset);
  header.set(file.filename, 46);
  return header;
}

function createEndOfCentralDirectoryRecord(fileCount: number, centralDirectorySize: number, centralDirectoryOffset: number): Uint8Array {
  const record = new Uint8Array(22);
  const view = new DataView(record.buffer);
  writeUint32(view, 0, 0x06054b50);
  writeUint16(view, 4, 0);
  writeUint16(view, 6, 0);
  writeUint16(view, 8, fileCount);
  writeUint16(view, 10, fileCount);
  writeUint32(view, 12, centralDirectorySize);
  writeUint32(view, 16, centralDirectoryOffset);
  writeUint16(view, 20, 0);
  return record;
}

function writeDosTimestamp(view: DataView, offset: number) {
  const now = new Date();
  const dosTime = (now.getHours() << 11) | (now.getMinutes() << 5) | Math.floor(now.getSeconds() / 2);
  const dosDate = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  writeUint16(view, offset, dosTime);
  writeUint16(view, offset + 2, dosDate);
}

function writeUint16(view: DataView, offset: number, value: number) {
  view.setUint16(offset, value, true);
}

function writeUint32(view: DataView, offset: number, value: number) {
  view.setUint32(offset, value >>> 0, true);
}

function buildCrcTable(): Uint32Array {
  const table = new Uint32Array(256);
  for (let i = 0; i < table.length; i += 1) {
    let value = i;
    for (let bit = 0; bit < 8; bit += 1) {
      value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    }
    table[i] = value >>> 0;
  }
  return table;
}

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc = crcTable[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function concatBytes(parts: Uint8Array[]): Uint8Array {
  const totalLength = parts.reduce((length, part) => length + part.byteLength, 0);
  const output = new Uint8Array(totalLength);
  let offset = 0;
  for (const part of parts) {
    output.set(part, offset);
    offset += part.byteLength;
  }
  return output;
}

function toArrayBuffer(bytes: Uint8Array): ArrayBuffer {
  const buffer = new ArrayBuffer(bytes.byteLength);
  new Uint8Array(buffer).set(bytes);
  return buffer;
}
