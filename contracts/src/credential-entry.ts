// OS credential store entries (SPEC.md section 5).

export const SERVICES = {
  production: 'com.marketcanopy.office',
  staging: 'com.marketcanopy.office.staging',
} as const;

export type Flavor = keyof typeof SERVICES;

export const MAX_GRANT_BYTES = 2048;

export interface GrantValue {
  version: 1;
  apiOrigin: string;
  userId: string;
  deviceSessionId: string;
  installationId: string;
  refreshToken: string;
  issuedAt: string;
  writtenBy: 'desktop' | 'office';
  lastActiveOrganizationId: string | null;
}

export function grantAccount(apiOrigin: string): string {
  return `grant|${apiOrigin}`;
}

export const INSTALLATION_ACCOUNT = 'installation';

/** The per-OS address of an entry. */
export function storeAddress(
  os: 'windows' | 'macos' | 'linux',
  flavor: Flavor,
  account: string,
): Record<string, string> {
  const service = SERVICES[flavor];
  switch (os) {
    case 'windows':
      return { type: 'CRED_TYPE_GENERIC', targetName: `${service}:${account}`, userName: account, persist: 'CRED_PERSIST_LOCAL_MACHINE' };
    case 'macos':
      return { class: 'kSecClassGenericPassword', service, account, label: 'Market Canopy Office' };
    case 'linux':
      return { schema: 'com.marketcanopy.office.Credential', service, account, label: 'Market Canopy Office' };
  }
}

export type ReadResult =
  | { state: 'present'; value: GrantValue }
  | { state: 'absent'; reason: 'missing' | 'unreadable' | 'other_version' | 'too_large' | 'invalid' };

const OBJECT_ID = /^[0-9a-f]{24}$/;
const UUID_V4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

/** Reads a stored grant value the way both apps must (5.2). */
export function readGrant(raw: string | null, apiOrigin: string): ReadResult {
  if (raw === null) return { state: 'absent', reason: 'missing' };
  if (Buffer.byteLength(raw, 'utf8') > MAX_GRANT_BYTES) return { state: 'absent', reason: 'too_large' };
  if (raw.charCodeAt(0) === 0xfeff) return { state: 'absent', reason: 'unreadable' };
  let value: unknown;
  try {
    value = JSON.parse(raw);
  } catch {
    return { state: 'absent', reason: 'unreadable' };
  }
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    return { state: 'absent', reason: 'unreadable' };
  }
  const v = value as Record<string, unknown>;
  if (v['version'] !== 1) return { state: 'absent', reason: 'other_version' };
  const ok =
    v['apiOrigin'] === apiOrigin &&
    typeof v['userId'] === 'string' && OBJECT_ID.test(v['userId']) &&
    typeof v['deviceSessionId'] === 'string' && OBJECT_ID.test(v['deviceSessionId']) &&
    typeof v['installationId'] === 'string' && UUID_V4.test(v['installationId']) &&
    typeof v['refreshToken'] === 'string' && v['refreshToken'].startsWith('dsr_') &&
    typeof v['issuedAt'] === 'string' && !Number.isNaN(Date.parse(v['issuedAt'])) &&
    (v['writtenBy'] === 'desktop' || v['writtenBy'] === 'office') &&
    (v['lastActiveOrganizationId'] === null ||
      (typeof v['lastActiveOrganizationId'] === 'string' && OBJECT_ID.test(v['lastActiveOrganizationId'])));
  if (!ok) return { state: 'absent', reason: 'invalid' };
  return {
    state: 'present',
    value: {
      version: 1,
      apiOrigin,
      userId: v['userId'] as string,
      deviceSessionId: v['deviceSessionId'] as string,
      installationId: v['installationId'] as string,
      refreshToken: v['refreshToken'] as string,
      issuedAt: v['issuedAt'] as string,
      writtenBy: v['writtenBy'] as 'desktop' | 'office',
      lastActiveOrganizationId: v['lastActiveOrganizationId'] as string | null,
    },
  };
}

/** Rule 5.4.3: may the desktop app write the grant entry now? */
export function desktopMayWrite(current: ReadResult, signedInUserId: string): boolean {
  if (current.state === 'absent') return true;
  return current.value.userId !== signedInUserId;
}

/** Rule 5.4.4: compare-and-delete. */
export function mayDelete(current: ReadResult, revokedDeviceSessionId: string): boolean {
  return current.state === 'present' && current.value.deviceSessionId === revokedDeviceSessionId;
}
