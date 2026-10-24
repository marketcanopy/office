// File locks (SPEC.md section 10).

import type { ErrorCode } from './errors.ts';

export type LockState = 'unlocked' | 'locked' | 'lock_file_only' | 'read_only';

export interface LockObservation {
  /** The OS lock check (sharing violation on Windows, F_GETLK on POSIX). */
  osLock: boolean;
  /** `.~lock.<name>#` exists beside the file. */
  lockFile: boolean;
  /** The person can write the file. */
  writable: boolean;
}

export function lockState(observation: LockObservation): LockState {
  if (observation.osLock) return 'locked';
  if (observation.lockFile) return 'lock_file_only';
  if (!observation.writable) return 'read_only';
  return 'unlocked';
}

export function writeDecision(
  state: LockState,
): { allowed: true } | { allowed: false; error: Extract<ErrorCode, 'DOCUMENT_OPEN_IN_OFFICE' | 'PATH_NOT_WRITABLE'> } {
  switch (state) {
    case 'unlocked':
      return { allowed: true };
    case 'locked':
    case 'lock_file_only':
      return { allowed: false, error: 'DOCUMENT_OPEN_IN_OFFICE' };
    case 'read_only':
      return { allowed: false, error: 'PATH_NOT_WRITABLE' };
  }
}

/** The lock file beside `path`, with the separator of `path`. */
export function lockFilePath(path: string): string {
  const index = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  const dir = path.slice(0, index + 1);
  const name = path.slice(index + 1);
  return `${dir}.~lock.${name}#`;
}

export interface LockFileContent {
  user: string;
  host: string;
  date: string;
  appDataDir: string;
}

/** `,<user>,<host>,<DD.MM.YYYY HH:MM>,<app data dir>;` (filelocker.cpp). Display only. */
export function parseLockFile(text: string): LockFileContent | null {
  const match = /^,([^,]*),([^,]*),(\d{2}\.\d{2}\.\d{4} \d{2}:\d{2}),([^;]*);$/.exec(text.trimEnd());
  if (!match) return null;
  const unescape = (value: string): string => value.replaceAll('&#39;', ',');
  return {
    user: unescape(match[1] ?? ''),
    host: unescape(match[2] ?? ''),
    date: match[3] ?? '',
    appDataDir: unescape(match[4] ?? ''),
  };
}
