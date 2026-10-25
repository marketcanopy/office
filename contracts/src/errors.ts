// Error codes of the Office contract (SPEC.md section 13).

export type OfficeState =
  | 'signed_out'
  | 'reconnecting'
  | 'retry_once'
  | 'pick_organization'
  | 'update_required'
  | 'not_entitled'
  | 'quota_exceeded'
  | 'retry_after'
  | 'reload_models'
  | 'too_large'
  | 'ai_unavailable'
  | 'bug'
  | 'close_in_office'
  | 'refused'
  | 'conflict'
  | 'offline';

export interface ErrorCodeInfo {
  /** HTTP status, or null when the code never crosses HTTP. */
  status: number | null;
  /** What Office (or the Canopy tool) shows. */
  state: OfficeState;
}

export const ERROR_CODES = {
  UNAUTHORIZED: { status: 401, state: 'signed_out' },
  GRANT_REVOKED: { status: 401, state: 'signed_out' },
  ACCOUNT_CHANGED: { status: 409, state: 'reconnecting' },
  CODE_EXPIRED: { status: 401, state: 'retry_once' },
  CODE_REPLAYED: { status: 401, state: 'retry_once' },
  WRONG_AUDIENCE: { status: 403, state: 'signed_out' },
  NOT_A_MEMBER: { status: 403, state: 'pick_organization' },
  CLIENT_UPGRADE_REQUIRED: { status: 426, state: 'update_required' },
  AI_NOT_ENTITLED: { status: 403, state: 'not_entitled' },
  QUOTA_EXCEEDED: { status: 429, state: 'quota_exceeded' },
  RATE_LIMITED: { status: 429, state: 'retry_after' },
  MODEL_NOT_ALLOWED: { status: 400, state: 'reload_models' },
  REQUEST_TOO_LARGE: { status: 413, state: 'too_large' },
  PROVIDER_UNAVAILABLE: { status: 503, state: 'ai_unavailable' },
  VALIDATION_ERROR: { status: 422, state: 'bug' },
  INVALID_CLIENT_HEADERS: { status: 400, state: 'bug' },
  DOCUMENT_OPEN_IN_OFFICE: { status: null, state: 'close_in_office' },
  PATH_OUTSIDE_GRANT: { status: null, state: 'refused' },
  PATH_NOT_WRITABLE: { status: null, state: 'refused' },
  VERSION_CONFLICT: { status: 409, state: 'conflict' },
  NETWORK_UNAVAILABLE: { status: null, state: 'offline' },
  CREDENTIAL_IN_LINK: { status: null, state: 'refused' },
  INVALID_LINK: { status: null, state: 'refused' },
  INVALID_ARGUMENT: { status: null, state: 'refused' },
} as const satisfies Record<string, ErrorCodeInfo>;

export type ErrorCode = keyof typeof ERROR_CODES;

export function isErrorCode(value: unknown): value is ErrorCode {
  return typeof value === 'string' && Object.hasOwn(ERROR_CODES, value);
}

/** The OpenAI error `type` the gateway sends for each of its codes. */
export const GATEWAY_ERROR_TYPES: Partial<Record<ErrorCode, string>> = {
  UNAUTHORIZED: 'authentication_error',
  GRANT_REVOKED: 'authentication_error',
  WRONG_AUDIENCE: 'permission_error',
  NOT_A_MEMBER: 'permission_error',
  CLIENT_UPGRADE_REQUIRED: 'invalid_request_error',
  AI_NOT_ENTITLED: 'permission_error',
  MODEL_NOT_ALLOWED: 'invalid_request_error',
  REQUEST_TOO_LARGE: 'invalid_request_error',
  RATE_LIMITED: 'rate_limit_error',
  QUOTA_EXCEEDED: 'insufficient_quota',
  PROVIDER_UNAVAILABLE: 'api_error',
  VALIDATION_ERROR: 'invalid_request_error',
};
