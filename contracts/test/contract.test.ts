import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { ERROR_CODES, GATEWAY_ERROR_TYPES, buildOpenLink, parseSchemeUrl } from '../src/index.ts';
import { contractHash } from '../src/hash-cli.ts';
import { ROOT } from '../src/schema.ts';

function schema(name: string): Record<string, unknown> {
  return JSON.parse(readFileSync(join(ROOT, 'schemas', `${name}.schema.json`), 'utf8')) as Record<string, unknown>;
}

test('error-code schema lists exactly the codes in src/errors.ts', () => {
  assert.deepEqual([...(schema('error-code')['enum'] as string[])].sort(), Object.keys(ERROR_CODES).sort());
});

test('gateway-error schema lists exactly the gateway codes', () => {
  const error = (schema('gateway-error')['properties'] as Record<string, { properties: Record<string, { enum: string[] }> }>)['error'];
  assert.deepEqual([...(error?.properties['code']?.enum ?? [])].sort(), Object.keys(GATEWAY_ERROR_TYPES).sort());
});

test('VERSION matches package.json', () => {
  const version = readFileSync(join(ROOT, 'VERSION'), 'utf8').trim();
  const pkg = JSON.parse(readFileSync(join(ROOT, 'package.json'), 'utf8')) as { version: string };
  assert.equal(pkg.version, version);
});

test('buildOpenLink produces a link parseSchemeUrl accepts', () => {
  const input = {
    name: 'Budget (Q3)',
    appOrigin: 'https://app.marketcanopy.com',
    documentId: '66f0c0ffee0000000000c001',
    organizationId: '66f0c0ffee0000000000b001',
  };
  assert.deepEqual(parseSchemeUrl(buildOpenLink(input)), { ok: true, kind: 'open', ...input });
  assert.throws(() => buildOpenLink({ ...input, name: 'a|b' }));
  assert.throws(() => buildOpenLink({ ...input, appOrigin: 'https://evil.example' }));
});

test('CONTRACT.sha256 matches the files', () => {
  const version = readFileSync(join(ROOT, 'VERSION'), 'utf8').trim();
  const recorded = readFileSync(join(ROOT, 'CONTRACT.sha256'), 'utf8').trim();
  assert.equal(recorded, `${contractHash()}  office-contracts-${version}`);
});
