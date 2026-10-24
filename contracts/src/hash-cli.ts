// Prints the contract hash, or with `--check` compares it to CONTRACT.sha256.
//
// SHA-256 over the contract files in byte order of their paths, each as
// `<path>\0<length>\0<bytes>`. Covered: SPEC.md, VERSION, LICENSE,
// package.json, schemas/, fixtures/ and src/. Not covered: tests, README,
// CHANGELOG and CONTRACT.sha256 itself.

import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { ROOT } from './schema.ts';

const FILES = ['SPEC.md', 'VERSION', 'LICENSE', 'package.json'];
const DIRECTORIES = ['schemas', 'fixtures', 'src'];

function walk(directory: string): string[] {
  return readdirSync(directory).flatMap((entry) => {
    const path = join(directory, entry);
    return statSync(path).isDirectory() ? walk(path) : [path];
  });
}

export function contractFiles(): string[] {
  const paths = [
    ...FILES.map((file) => join(ROOT, file)),
    ...DIRECTORIES.flatMap((directory) => walk(join(ROOT, directory))),
  ].map((path) => relative(ROOT, path).split(sep).join('/'));
  return paths.sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
}

export function contractHash(): string {
  const hash = createHash('sha256');
  for (const path of contractFiles()) {
    const bytes = readFileSync(join(ROOT, path));
    hash.update(`${path}\0${bytes.length}\0`);
    hash.update(bytes);
  }
  return hash.digest('hex');
}

function main(): void {
  const version = readFileSync(join(ROOT, 'VERSION'), 'utf8').trim();
  const digest = contractHash();
  const line = `${digest}  office-contracts-${version}`;
  if (process.argv.includes('--check')) {
    const recorded = readFileSync(join(ROOT, 'CONTRACT.sha256'), 'utf8').trim();
    if (recorded !== line) {
      console.error(`contract hash mismatch\n recorded: ${recorded}\n computed: ${line}`);
      process.exit(1);
    }
    console.log(`contract hash matches: ${line}`);
    return;
  }
  console.log(line);
}

if (process.argv[1] !== undefined && import.meta.url === new URL(`file://${process.argv[1]}`).href) main();
