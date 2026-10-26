// Office URL scheme (SPEC.md section 4).

import type { ErrorCode } from './errors.ts';

export const SCHEME = 'mc-office';

export const ALLOWED_APP_ORIGINS = [
  'https://app.marketcanopy.com',
] as const;

const CREDENTIAL_PARAMS = new Set([
  'token',
  'access_token',
  'refresh_token',
  'id_token',
  'code',
  'ticket',
  'key',
  'apikey',
  'api_key',
  'secret',
  'password',
  'auth',
  'authorization',
  'jwt',
  'session',
  'sig',
  'signature',
]);

const OBJECT_ID = /^[0-9a-f]{24}$/;
const BASE64URL_43 = /^[A-Za-z0-9_-]{43}$/;
const ERROR_CODE = /^[A-Z][A-Z0-9_]{0,63}$/;
const NAME_ALLOWED = /^[^|\u0000-\u001f]{1,255}$/;

export type SchemeUrl =
  | {
      ok: true;
      kind: 'open';
      name: string;
      appOrigin: string;
      documentId: string;
      organizationId: string;
    }
  | { ok: true; kind: 'auth_callback'; code: string; state: string }
  | { ok: true; kind: 'auth_error'; error: string; state: string }
  | { ok: false; error: Extract<ErrorCode, 'INVALID_LINK' | 'CREDENTIAL_IN_LINK'> };

const invalid = { ok: false, error: 'INVALID_LINK' } as const;
const credential = { ok: false, error: 'CREDENTIAL_IN_LINK' } as const;

function looksLikeCredential(value: string): boolean {
  return (
    value.startsWith('dsr_') ||
    /^Bearer\s/i.test(value) ||
    /^[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*$/.test(value)
  );
}

function carriesCredential(params: URLSearchParams): boolean {
  for (const [name, value] of params) {
    if (CREDENTIAL_PARAMS.has(name.toLowerCase())) return true;
    if (looksLikeCredential(value)) return true;
  }
  return false;
}

function parseDocumentUrl(raw: string): SchemeUrl {
  let url: URL;
  try {
    url = new URL(raw);
  } catch {
    return invalid;
  }
  if (carriesCredential(url.searchParams)) return credential;
  if (url.protocol !== 'https:' || url.username || url.password || url.port || url.hash) {
    return invalid;
  }
  if (!(ALLOWED_APP_ORIGINS as readonly string[]).includes(url.origin)) return invalid;
  const match = /^\/office\/documents\/([0-9a-f]{24})$/.exec(url.pathname);
  if (!match) return invalid;
  const keys = [...url.searchParams.keys()];
  const extra = keys.filter((key) => key !== 'organizationId' && key !== 'desktop');
  if (extra.length > 0) return invalid;
  if (url.searchParams.getAll('organizationId').length !== 1) return invalid;
  if (url.searchParams.has('desktop') && url.searchParams.get('desktop') !== 'true') return invalid;
  const organizationId = url.searchParams.get('organizationId') ?? '';
  if (!OBJECT_ID.test(organizationId)) return invalid;
  return {
    ok: true,
    kind: 'open',
    name: '',
    appOrigin: url.origin,
    documentId: match[1] ?? '',
    organizationId,
  };
}

/** Parses one `mc-office:` URL. Nothing else is accepted. */
export function parseSchemeUrl(input: string): SchemeUrl {
  if (typeof input !== 'string' || input.length > 4096) return invalid;
  if (/[\u0000-\u001f]/.test(input)) return invalid;
  const prefix = `${SCHEME}://`;
  if (!input.startsWith(prefix)) return invalid;
  const rest = input.slice(prefix.length).replace(/%7C/gi, '|');

  if (rest.startsWith('open|')) {
    const fields = rest.split('|');
    // open | n | <name> | <documentUrl>
    if (fields.length !== 4 || fields[1] !== 'n') {
      // Still refuse a credential anywhere in a malformed link first.
      if (/[?&](token|access_token|refresh_token|code|ticket)=/i.test(rest)) return credential;
      return invalid;
    }
    const result = parseDocumentUrl(fields[3] ?? '');
    if (!result.ok) return result;
    let name: string;
    try {
      name = decodeURIComponent(fields[2] ?? '');
    } catch {
      return invalid;
    }
    if (!NAME_ALLOWED.test(name)) return invalid;
    return result.kind === 'open' ? { ...result, name } : invalid;
  }

  if (rest.startsWith('auth/callback?')) {
    const params = new URLSearchParams(rest.slice('auth/callback?'.length));
    const keys = [...params.keys()].sort().join(',');
    const state = params.get('state') ?? '';
    if (!BASE64URL_43.test(state)) return invalid;
    if (keys === 'code,state') {
      const code = params.get('code') ?? '';
      return BASE64URL_43.test(code) ? { ok: true, kind: 'auth_callback', code, state } : invalid;
    }
    if (keys === 'error,state') {
      const error = params.get('error') ?? '';
      return ERROR_CODE.test(error) ? { ok: true, kind: 'auth_error', error, state } : invalid;
    }
    return invalid;
  }

  // `//action|…`, `//command/…`, plugin-store links and anything else.
  return invalid;
}

/** Builds the only link a producer may send (SPEC.md 4.1). */
export function buildOpenLink(input: {
  name: string;
  appOrigin: string;
  documentId: string;
  organizationId: string;
}): string {
  const url = `${input.appOrigin}/office/documents/${input.documentId}?organizationId=${input.organizationId}`;
  const link = `${SCHEME}://open|n|${encodeURIComponent(input.name)}|${url}`;
  const parsed = parseSchemeUrl(link);
  if (!parsed.ok) throw new Error(`refusing to build an invalid link (${parsed.error})`);
  return link;
}
