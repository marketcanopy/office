// Runs every fixture in `fixtures/` against the schemas and the reference
// implementation in `src/`.

import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, test } from 'node:test';
import {
  CLIENT_VERSION_PATTERN,
  ERROR_CODES,
  GATEWAY_ERROR_TYPES,
  assembleStream,
  compareVersions,
  desktopMayWrite,
  isErrorCode,
  lockFilePath,
  lockState,
  mayDelete,
  parseLockFile,
  parseRecents,
  parseSchemeUrl,
  readGrant,
  toClientVersionHeader,
  validate,
  validateLaunchArgs,
  writeDecision,
} from '../src/index.ts';
import { ROOT } from '../src/schema.ts';

type Json = Record<string, unknown>;

interface Fixture {
  fixture: 1;
  name: string;
  description: string;
  subject: string;
  input: unknown;
  expect: unknown;
}

const FIXTURES = join(ROOT, 'fixtures');

function load(): { folder: string; fixture: Fixture }[] {
  return readdirSync(FIXTURES).sort().flatMap((folder) =>
    readdirSync(join(FIXTURES, folder))
      .filter((file) => file.endsWith('.json'))
      .sort()
      .map((file) => ({ folder, fixture: JSON.parse(readFileSync(join(FIXTURES, folder, file), 'utf8')) as Fixture })),
  );
}

function assertValid(schema: string, value: unknown): void {
  const result = validate(schema, value);
  assert.ok(result.valid, `${schema}: ${result.errors.join('; ')}`);
}

const OFFICE_APPS = new Set(['com.marketcanopy.office', 'com.marketcanopy.desktop']);
const GATEWAY_PREFIX = '/v1/office/ai/';
/** Routes the dashboard or the connect pages' server call, without Office's client headers. */
const WEB_CALLED = new Set([
  '/v1/auth/native/handoffs',
  '/v1/office/web-session/redeem',
  '/v1/office/web-session/token',
  '/v1/office/web-session/logout',
]);
const WEB_COOKIE = '__Host-mc_office_web';

function header(headers: Json | undefined, name: string): string | undefined {
  if (headers === undefined) return undefined;
  const key = Object.keys(headers).find((candidate) => candidate.toLowerCase() === name.toLowerCase());
  return key === undefined ? undefined : String(headers[key]);
}

/** The client headers every envelope route expects (SPEC.md 6). */
function clientHeadersValid(headers: Json | undefined): boolean {
  const app = header(headers, 'X-Client-App');
  const version = header(headers, 'X-Client-Version');
  return (
    app !== undefined && OFFICE_APPS.has(app) &&
    ['windows', 'macos', 'linux'].includes(header(headers, 'X-Client-Platform') ?? '') &&
    version !== undefined && CLIENT_VERSION_PATTERN.test(version) &&
    /^[0-9a-f-]{36}$/.test(header(headers, 'X-Device-Id') ?? '')
  );
}

const exchanges = new Map<string, Json>();

function checkExchange(folder: string, fixture: Fixture): void {
  const input = fixture.input as { request: Json; response: Json };
  const expect = fixture.expect as Json;
  const { request, response } = input;
  const path = String(request['path']);
  assert.ok(path.startsWith('/v1/'), 'paths are /v1 routes');
  assert.ok(!/[?&](token|code|refresh_token|access_token)=/i.test(path), 'no credential in a request path');

  if (typeof request['schema'] === 'string') {
    assertValid(request['schema'], request['body']);
  }
  if (expect['requestValid'] === false) {
    assert.equal(typeof request['schema'], 'object', 'an invalid request names no schema');
  }

  const gateway = path.startsWith(GATEWAY_PREFIX);
  const webCalled = WEB_CALLED.has(path);
  if (!gateway && !webCalled) {
    const headersValid = clientHeadersValid(request['headers'] as Json | undefined);
    assert.equal(headersValid, expect['headersValid'] !== false, 'client headers');
  }
  if (gateway) {
    const authorization = header(request['headers'] as Json, 'Authorization') ?? '';
    assert.ok(authorization.startsWith('Bearer '), 'gateway requests carry a bearer token');
    assert.ok(/^[0-9a-f]{24}$/.test(header(request['headers'] as Json, 'x-active-organization-id') ?? ''), 'gateway requests name the organization');
  }

  const cookie = header(request['headers'] as Json | undefined, 'Cookie');
  if (cookie !== undefined) {
    assert.ok(path.startsWith('/v1/office/web-session/'), 'only the web-session routes take the cookie');
    assert.match(cookie, new RegExp(`^${WEB_COOKIE}=[^;]+$`), 'the page forwards the web-session cookie and nothing else');
  }

  const status = response['status'] as number;
  const body = response['body'] as Json | undefined;
  const headers = response['headers'] as Json | undefined;

  if (expect['outcome'] === 'ok') {
    assert.ok(status >= 200 && status < 300, `status ${status}`);
    if (gateway) {
      if (response['sse'] !== undefined) {
        assert.equal(header(headers, 'Content-Type'), 'text/event-stream');
      } else {
        assertValid(String(response['schema']), body);
      }
    } else {
      assertValid('envelope-success', body);
      assertValid(String(response['schema']), body?.['data']);
    }
    if (typeof expect['cookie'] === 'string') {
      const cookie = header(headers, 'Set-Cookie') ?? '';
      assert.ok(cookie.startsWith(`${expect['cookie']}=`), 'cookie name');
      for (const attribute of ['Path=/', 'Secure', 'HttpOnly', 'SameSite=Lax', 'Max-Age=28800']) {
        assert.ok(cookie.split('; ').includes(attribute), `cookie ${attribute}`);
      }
      assert.ok(!cookie.includes('Domain='), '__Host- cookies carry no Domain');
      assert.ok(!/asc_auth_key/.test(cookie), 'never the cookie upstream exposes to plugins');
    }
    if (typeof expect['clearsCookie'] === 'string') {
      const cleared = (header(headers, 'Set-Cookie') ?? '').split('; ');
      assert.equal(cleared[0], `${expect['clearsCookie']}=`, 'cookie emptied');
      for (const attribute of ['Path=/', 'Secure', 'HttpOnly', 'SameSite=Lax', 'Max-Age=0']) {
        assert.ok(cleared.includes(attribute), `cleared cookie ${attribute}`);
      }
    }
    const data = gateway ? undefined : (body?.['data'] as Json | undefined);
    if (typeof data?.['redirectUri'] === 'string') {
      const callback = parseSchemeUrl(data['redirectUri']);
      assert.ok(callback.ok && callback.kind === 'auth_callback', 'the redirect is the sign-in callback');
      assert.equal(callback.state, (request['body'] as Json)['state'], 'the API echoes Office\'s state');
    }
    if (typeof expect['sameAs'] === 'string') {
      const first = exchanges.get(`${folder}/${expect['sameAs']}`);
      assert.ok(first !== undefined, 'the fixture it repeats');
      const data = body?.['data'] as Json;
      for (const field of expect['sameFields'] as string[]) assert.equal(data[field], first[field], field);
      assert.notEqual(data['refreshToken'], first['refreshToken'], 'a repeat rotates the refresh token');
    }
    if (typeof expect['maxLifetimeSeconds'] === 'number') {
      const data = body?.['data'] as Json;
      const lifetime = (Date.parse(String(data['expiresAt'])) - Date.parse(String(expect['issuedAt']))) / 1000;
      assert.ok(lifetime > 0 && lifetime <= expect['maxLifetimeSeconds'], `lifetime ${lifetime}s`);
    }
    if (!gateway && body !== undefined) exchanges.set(`${folder}/${fixture.name}`, body['data'] as Json);
    return;
  }

  const code = String(expect['outcome']);
  assert.ok(isErrorCode(code), `${code} is a contract error code`);
  assert.equal(status, ERROR_CODES[code].status, `HTTP status of ${code}`);
  if (gateway) {
    assertValid('gateway-error', body);
    const error = body?.['error'] as Json;
    assert.equal(error['code'], code);
    assert.equal(error['type'], GATEWAY_ERROR_TYPES[code], `type of ${code}`);
    if (code === 'MODEL_NOT_ALLOWED') assert.equal(error['param'], 'model');
    if (code === 'QUOTA_EXCEEDED') assert.equal(header(headers, 'x-should-retry'), 'false');
    if (code === 'RATE_LIMITED') assert.match(header(headers, 'Retry-After') ?? '', /^\d+$/);
  } else {
    assertValid('envelope-error', body);
    const error = body?.['error'] as Json;
    assert.equal(error['code'], code);
    assert.equal((body?.['meta'] as Json)['path'], path, 'meta.path is the request path');
  }
  const text = JSON.stringify(body);
  assert.ok(!/dsr_|eyJ/.test(text), 'an error never carries a token');
}

function check(folder: string, fixture: Fixture): void {
  const expect = fixture.expect as Json;
  switch (fixture.subject) {
    case 'launch-args':
      assert.deepEqual(stripReason(validateLaunchArgs(fixture.input as string[])), expect);
      return;
    case 'deep-link':
      assert.deepEqual(parseSchemeUrl(fixture.input as string), expect);
      return;
    case 'credential-read': {
      const input = fixture.input as { raw: string | null; apiOrigin: string; signedInUserId?: string; revokedDeviceSessionId?: string };
      const read = readGrant(input.raw, input.apiOrigin);
      const actual: Json = { ...read };
      if (input.signedInUserId !== undefined) actual['desktopMayWrite'] = desktopMayWrite(read, input.signedInUserId);
      if (input.revokedDeviceSessionId !== undefined) actual['mayDelete'] = mayDelete(read, input.revokedDeviceSessionId);
      assert.deepEqual(actual, expect);
      if (read.state === 'present') assertValid('credential-grant', read.value);
      return;
    }
    case 'document': {
      const input = fixture.input as { schema: string; value: unknown };
      const result = validate(input.schema, input.value);
      assert.equal(result.valid, expect['valid'], result.errors.join('; '));
      return;
    }
    case 'exchange':
      checkExchange(folder, fixture);
      return;
    case 'sse': {
      const result = assembleStream(fixture.input as string);
      assert.deepEqual(result, expect);
      if (result.ok) {
        for (const line of (fixture.input as string).split('\n')) {
          if (!line.startsWith('data: ') || line === 'data: [DONE]') continue;
          const event = JSON.parse(line.slice(6)) as Json;
          assertValid(event['error'] === undefined ? 'gateway-chat-chunk' : 'gateway-error', event);
        }
      }
      return;
    }
    case 'lock-state': {
      const state = lockState(fixture.input as Parameters<typeof lockState>[0]);
      assert.deepEqual({ state, write: writeDecision(state) }, expect);
      assertValid('lock-state', state);
      return;
    }
    case 'lock-file': {
      const input = fixture.input as { file?: string; text?: string; path: string };
      const text = input.file !== undefined ? readFileSync(join(FIXTURES, folder, input.file), 'utf8') : (input.text ?? '');
      assert.deepEqual({ lockFilePath: lockFilePath(input.path), content: parseLockFile(text) }, expect);
      return;
    }
    case 'recents': {
      const input = fixture.input as { file?: string; text?: string | null };
      const text = input.file !== undefined ? readFileSync(join(FIXTURES, folder, input.file), 'utf8') : (input.text ?? null);
      const result = parseRecents(text);
      assert.deepEqual(result, expect);
      if (result.state === 'ok') for (const file of result.files) assertValid('recent-file', file);
      return;
    }
    case 'client-version': {
      const input = fixture.input as { release?: string; compare?: [string, string] };
      if (input.release !== undefined) {
        const value = toClientVersionHeader(input.release);
        assert.deepEqual({ header: value }, expect);
        if (value !== null) assert.match(value, CLIENT_VERSION_PATTERN);
      } else {
        const [a, b] = input.compare ?? ['', ''];
        assert.deepEqual({ result: compareVersions(a, b) }, expect);
      }
      return;
    }
    default:
      assert.fail(`unknown subject ${fixture.subject}`);
  }
}

function stripReason<T extends object>(value: T): Omit<T, 'reason'> {
  const { reason: _reason, ...rest } = value as T & { reason?: unknown };
  return rest;
}

const all = load();

describe('fixtures', () => {
  test('every fixture matches the fixture schema and its file name', () => {
    assert.ok(all.length > 0);
    for (const { folder, fixture } of all) {
      assertValid('fixture', fixture);
      assert.ok(readdirSync(join(FIXTURES, folder)).includes(`${fixture.name}.json`), `${folder}/${fixture.name}`);
    }
  });

  for (const { folder, fixture } of all) {
    test(`${folder}/${fixture.name}: ${fixture.description}`, () => check(folder, fixture));
  }
});
