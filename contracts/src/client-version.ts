// Office release versions and the X-Client-Version header (SPEC.md 6.1).

/** The API's `CLIENT_VERSION_PATTERN`. */
export const CLIENT_VERSION_PATTERN = /^\d{1,6}(\.\d{1,6}){0,3}$/;

/** `<major>.<minor>.<patch>-mc.<n>` (ADR 0529 decision 3). */
export const RELEASE_VERSION_PATTERN = /^(\d{1,6})\.(\d{1,6})\.(\d{1,6})-mc\.(\d{1,6})$/;

/** `10.0.0-mc.3` becomes `10.0.0.3`; anything else is null. */
export function toClientVersionHeader(release: string): string | null {
  const match = RELEASE_VERSION_PATTERN.exec(release);
  if (!match) return null;
  return `${match[1]}.${match[2]}.${match[3]}.${match[4]}`;
}

function parts(version: string): number[] | null {
  const header = RELEASE_VERSION_PATTERN.test(version)
    ? toClientVersionHeader(version)
    : version;
  if (header === null || !CLIENT_VERSION_PATTERN.test(header)) return null;
  const numbers = header.split('.').map(Number);
  while (numbers.length < 4) numbers.push(0);
  return numbers;
}

/** Numeric comparison, part by part; null when either is malformed. */
export function compareVersions(a: string, b: string): -1 | 0 | 1 | null {
  const left = parts(a);
  const right = parts(b);
  if (left === null || right === null) return null;
  for (let i = 0; i < 4; i += 1) {
    const x = left[i] ?? 0;
    const y = right[i] ?? 0;
    if (x !== y) return x < y ? -1 : 1;
  }
  return 0;
}
