/*
 * Market Canopy Office: what the agent tells the person when AI cannot answer.
 * Copyright (C) 2026 Market Canopy LLC
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License version 3 as
 * published by the Free Software Foundation, together with the additional
 * terms in the LICENSE file of https://github.com/marketcanopy/office.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * AGPL at https://www.gnu.org/licenses/agpl-3.0.html.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

import type { MarketCanopyUnavailableState } from "./session";

// One sentence per session state (contract section 9) and per gateway error
// code (section 8.3). Never a token, a URL or a provider's own message.

export const STATE_MESSAGES: Record<MarketCanopyUnavailableState, string> = {
  signed_out: "Sign in to Market Canopy to use AI.",
  offline: "AI needs a network connection.",
  update_required: "Update Market Canopy Office to use AI.",
  not_entitled: "Your Market Canopy plan does not include AI.",
};

const CODE_MESSAGES: Record<string, string> = {
  UNAUTHORIZED: "Sign in to Market Canopy to use AI.",
  GRANT_REVOKED: "Sign in to Market Canopy to use AI.",
  WRONG_AUDIENCE: "Sign in to Market Canopy to use AI.",
  NOT_A_MEMBER:
    "You are no longer a member of this organization. Choose another organization in Market Canopy.",
  CLIENT_UPGRADE_REQUIRED: "Update Market Canopy Office to use AI.",
  AI_NOT_ENTITLED: "Your Market Canopy plan does not include AI.",
  MODEL_NOT_ALLOWED: "That model is not available. Choose another model.",
  REQUEST_TOO_LARGE:
    "This request is too large. Attach less content and try again.",
  RATE_LIMITED: "Too many AI requests. Wait a minute and try again.",
  QUOTA_EXCEEDED: "Your organization has used its AI allowance.",
  PROVIDER_UNAVAILABLE: "AI is unavailable right now. Try again shortly.",
  VALIDATION_ERROR: "AI cannot handle this request.",
};

export const ACCOUNT_CHANGED_MESSAGE =
  "Your Market Canopy account or organization changed. Send the message again.";

export const OTHER_PROVIDER_MESSAGE =
  "Market Canopy Office uses Market Canopy AI only.";

const GENERIC_MESSAGE = "AI is unavailable right now. Try again shortly.";

/**
 * An error raised before any request is sent, because the session is not
 * ready. The message is the one the person sees.
 */
export class MarketCanopyUnavailableError extends Error {
  readonly state: MarketCanopyUnavailableState;

  constructor(state: MarketCanopyUnavailableState) {
    super(STATE_MESSAGES[state]);
    this.name = "MarketCanopyUnavailableError";
    this.state = state;
  }
}

/** The request was cancelled because the account or organization changed. */
export class MarketCanopyAccountChangedError extends Error {
  constructor() {
    super(ACCOUNT_CHANGED_MESSAGE);
    this.name = "MarketCanopyAccountChangedError";
  }
}

const gatewayCode = (error: object): string | undefined => {
  const record = error as Record<string, unknown>;
  if (typeof record.code === "string") return record.code;
  const nested = record.error;
  if (nested && typeof nested === "object") {
    const code = (nested as Record<string, unknown>).code;
    if (typeof code === "string") return code;
  }
  return undefined;
};

const gatewayParam = (error: object): string | undefined => {
  const record = error as Record<string, unknown>;
  if (typeof record.param === "string") return record.param;
  const nested = record.error;
  if (nested && typeof nested === "object") {
    const param = (nested as Record<string, unknown>).param;
    if (typeof param === "string") return param;
  }
  return undefined;
};

/**
 * The sentence to show for an error thrown by a request: a session state, a
 * gateway error code, an account change, or anything else.
 */
export const messageForError = (error: unknown): string => {
  if (
    error instanceof MarketCanopyUnavailableError ||
    error instanceof MarketCanopyAccountChangedError
  ) {
    return error.message;
  }
  if (error && typeof error === "object") {
    if (
      "cause" in error &&
      (error.cause instanceof MarketCanopyUnavailableError ||
        error.cause instanceof MarketCanopyAccountChangedError)
    ) {
      return error.cause.message;
    }
    const code = gatewayCode(error);
    if (code && CODE_MESSAGES[code]) {
      const param = code === "VALIDATION_ERROR" ? gatewayParam(error) : "";
      return param ? `${CODE_MESSAGES[code]} (${param})` : CODE_MESSAGES[code];
    }
    const status = (error as Record<string, unknown>).status;
    if (status === 401) return CODE_MESSAGES.UNAUTHORIZED;
    if (status === 413) return CODE_MESSAGES.REQUEST_TOO_LARGE;
    if (status === 426) return CODE_MESSAGES.CLIENT_UPGRADE_REQUIRED;
    if (status === 429) return CODE_MESSAGES.RATE_LIMITED;
  }
  return GENERIC_MESSAGE;
};
