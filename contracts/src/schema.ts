// Loads every schema in `schemas/` into one Ajv instance (draft 2020-12).

import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Ajv2020 } from 'ajv/dist/2020.js';
import addFormatsModule from 'ajv-formats';

export const ROOT = fileURLToPath(new URL('..', import.meta.url));
export const SCHEMA_BASE = 'https://contracts.marketcanopy.com/office/v1/';

const addFormats = addFormatsModule as unknown as (ajv: Ajv2020) => Ajv2020;

let instance: Ajv2020 | null = null;

export function schemaNames(): string[] {
  return readdirSync(join(ROOT, 'schemas'))
    .filter((file) => file.endsWith('.schema.json'))
    .map((file) => file.slice(0, -'.schema.json'.length))
    .sort();
}

export function ajv(): Ajv2020 {
  if (instance !== null) return instance;
  const created = new Ajv2020({ allErrors: true, strict: true, strictTypes: false, allowUnionTypes: true });
  addFormats(created);
  for (const name of schemaNames()) {
    const schema: unknown = JSON.parse(readFileSync(join(ROOT, 'schemas', `${name}.schema.json`), 'utf8'));
    created.addSchema(schema as object);
  }
  instance = created;
  return created;
}

export interface Validation {
  valid: boolean;
  errors: string[];
}

/** Validates `value` against `schemas/<name>.schema.json`. */
export function validate(name: string, value: unknown): Validation {
  const check = ajv().getSchema(`${SCHEMA_BASE}${name}.schema.json`);
  if (check === undefined) throw new Error(`unknown schema ${name}`);
  const valid = check(value) as boolean;
  return {
    valid,
    errors: (check.errors ?? []).map((error) => `${error.instancePath || '/'} ${error.message ?? ''}`.trim()),
  };
}
