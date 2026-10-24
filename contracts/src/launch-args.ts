// Launch arguments (SPEC.md section 3).

import { parseSchemeUrl, SCHEME } from './deep-link.ts';
import type { ErrorCode } from './errors.ts';

export const NEW_DOCUMENT_ARGS = ['--new:word', '--new:cell', '--new:slide'] as const;

export const OPENABLE_EXTENSIONS = [
  'docx', 'doc', 'rtf', 'odt', 'txt',
  'xlsx', 'xls', 'csv', 'ods',
  'pptx', 'ppt', 'odp',
  'pdf',
] as const;

export type LaunchArgsResult =
  | { ok: true }
  | { ok: false; error: Extract<ErrorCode, 'INVALID_ARGUMENT' | 'CREDENTIAL_IN_LINK'>; index: number; reason: string };

function isAbsolutePath(value: string): boolean {
  return value.startsWith('/') || /^[A-Za-z]:\\/.test(value) || /^\\\\[^\\]+\\[^\\]+\\/.test(value);
}

function validatePath(value: string): string | null {
  if (!isAbsolutePath(value)) return 'not an absolute path';
  const segments = value.split(/[\\/]/);
  if (segments.some((segment) => segment === '.' || segment === '..')) return 'dot segment';
  const name = segments[segments.length - 1] ?? '';
  const dot = name.lastIndexOf('.');
  const extension = dot > 0 ? name.slice(dot + 1).toLowerCase() : '';
  if (!(OPENABLE_EXTENSIONS as readonly string[]).includes(extension)) return 'extension not offered';
  return null;
}

/** Validates the argument array the desktop app passes to Office. */
export function validateLaunchArgs(args: readonly string[]): LaunchArgsResult {
  if (args.length === 0) return { ok: true };
  let links = 0;
  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index] ?? '';
    const fail = (reason: string): LaunchArgsResult => ({ ok: false, error: 'INVALID_ARGUMENT', index, reason });
    if (arg.length === 0) return fail('empty');
    if (Buffer.byteLength(arg, 'utf8') > 4096) return fail('too long');
    if (/[\u0000\u0001\r\n]/.test(arg)) return fail('control character');
    if ((NEW_DOCUMENT_ARGS as readonly string[]).includes(arg)) continue;
    if (arg.startsWith('-')) return fail('option not allowed');
    if (arg.startsWith(`${SCHEME}:`)) {
      links += 1;
      if (links > 1) return fail('more than one link');
      const parsed = parseSchemeUrl(arg);
      if (!parsed.ok) {
        return parsed.error === 'CREDENTIAL_IN_LINK'
          ? { ok: false, error: 'CREDENTIAL_IN_LINK', index, reason: 'credential in link' }
          : fail('invalid link');
      }
      if (parsed.kind !== 'open') return fail('only open links may be passed');
      continue;
    }
    if (/^(https?|file):/i.test(arg)) return fail('URL not allowed');
    const problem = validatePath(arg);
    if (problem !== null) return fail(problem);
  }
  return { ok: true };
}
