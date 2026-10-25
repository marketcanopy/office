/*
 * Market Canopy Office: the AI session the agent reads from native code.
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

// window.AscDesktopEditor.getMarketCanopyAiSession() (Office contract section
// 9) is the only source of the token, the base URL and the model aliases.
// Native code keeps the token in memory and refreshes it; callers read the
// method for every request and never keep the token.

export type MarketCanopyCapability = "chat" | "tools" | "vision" | "reasoning";

export type MarketCanopyModel = {
  id: string;
  name: string;
  capabilities: MarketCanopyCapability[];
};

export type MarketCanopyReadySession = {
  state: "ready";
  baseUrl: string;
  accessToken: string;
  expiresAt: string;
  organizationId: string;
  models: MarketCanopyModel[];
  defaultModel: string;
};

export type MarketCanopyUnavailableState =
  | "signed_out"
  | "offline"
  | "update_required"
  | "not_entitled";

export type MarketCanopySession =
  | MarketCanopyReadySession
  | { state: MarketCanopyUnavailableState };

const UNAVAILABLE_STATES: MarketCanopyUnavailableState[] = [
  "signed_out",
  "offline",
  "update_required",
  "not_entitled",
];

const BASE_URL = /^https:\/\/[a-z0-9.-]+\/v1\/office\/ai$/;
const MODEL_ALIAS = /^[a-z0-9][a-z0-9.-]{0,63}$/;
const CAPABILITIES: MarketCanopyCapability[] = [
  "chat",
  "tools",
  "vision",
  "reasoning",
];

type SessionHost = { getMarketCanopyAiSession?: () => unknown };

const isString = (value: unknown): value is string =>
  typeof value === "string" && value.length > 0;

const parseModel = (value: unknown): MarketCanopyModel | null => {
  if (!value || typeof value !== "object") return null;
  const model = value as Record<string, unknown>;
  if (!isString(model.id) || !MODEL_ALIAS.test(model.id)) return null;
  if (!isString(model.name)) return null;
  const capabilities = Array.isArray(model.capabilities)
    ? model.capabilities.filter((item): item is MarketCanopyCapability =>
        CAPABILITIES.includes(item as MarketCanopyCapability)
      )
    : [];
  return { id: model.id, name: model.name, capabilities };
};

/**
 * Validates a getMarketCanopyAiSession() result. Anything that is not a
 * well-formed ready session or a known state reads as "offline", so a broken
 * answer never reaches a request.
 */
export const parseSession = (raw: unknown): MarketCanopySession => {
  let value: unknown = raw;
  if (typeof raw === "string") {
    try {
      value = JSON.parse(raw);
    } catch {
      return { state: "offline" };
    }
  }
  if (!value || typeof value !== "object") return { state: "offline" };
  const session = value as Record<string, unknown>;

  if (session.state !== "ready") {
    return UNAVAILABLE_STATES.includes(
      session.state as MarketCanopyUnavailableState
    )
      ? { state: session.state as MarketCanopyUnavailableState }
      : { state: "offline" };
  }

  const models = Array.isArray(session.models)
    ? session.models
        .map(parseModel)
        .filter((model): model is MarketCanopyModel => model !== null)
    : [];
  if (
    !isString(session.baseUrl) ||
    !BASE_URL.test(session.baseUrl) ||
    !isString(session.accessToken) ||
    !isString(session.expiresAt) ||
    !isString(session.organizationId) ||
    !isString(session.defaultModel) ||
    models.length === 0
  ) {
    return { state: "offline" };
  }

  return {
    state: "ready",
    baseUrl: session.baseUrl,
    accessToken: session.accessToken,
    expiresAt: session.expiresAt,
    organizationId: session.organizationId,
    models,
    defaultModel: models.some((model) => model.id === session.defaultModel)
      ? session.defaultModel
      : models[0].id,
  };
};

/**
 * Reads the session now. Outside Office (no native method) the person is
 * treated as signed out.
 */
export const readSession = (): MarketCanopySession => {
  const host =
    typeof window === "undefined"
      ? undefined
      : (window.AscDesktopEditor as SessionHost | undefined);
  if (!host || typeof host.getMarketCanopyAiSession !== "function") {
    return { state: "signed_out" };
  }
  try {
    return parseSession(host.getMarketCanopyAiSession());
  } catch {
    return { state: "offline" };
  }
};

/**
 * What identifies the account, organization and models a session belongs to,
 * without the token: two sessions with the same key need no reload.
 */
export const sessionKey = (session: MarketCanopySession): string =>
  session.state === "ready"
    ? [
        session.state,
        session.baseUrl,
        session.organizationId,
        session.models.map((model) => model.id).join(","),
      ].join("|")
    : session.state;
