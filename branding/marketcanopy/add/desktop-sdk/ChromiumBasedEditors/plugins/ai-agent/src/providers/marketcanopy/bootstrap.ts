/*
 * Market Canopy Office: the agent's stored settings before any store loads.
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

// main.tsx imports this module first, so it runs before the zustand stores
// read localStorage. It puts the managed provider first in the provider list
// and selects it unless the person selected one of their own providers
// (ADR 0525 amendment 2026-10-26: people may add upstream's providers with
// their own keys). It drops stdio MCP servers (native code no longer starts
// processes). Web search is upstream's, with the person's own Exa key.

import type { Model, TProvider } from "@/lib/types";
import { MARKETCANOPY_PROVIDER } from "./constants";
import { withoutStdioServers } from "./mcp";
import { type MarketCanopySession, readSession } from "./session";

export const PROVIDERS_KEY = "providers";
export const CURRENT_PROVIDER_KEY = "current-provider";
export const CURRENT_MODEL_KEY = "current-model";
export const MCP_SERVERS_KEY = "mcpServers";
export const LAST_MODELS_KEY = "marketcanopy-models";

const PROVIDER: TProvider = MARKETCANOPY_PROVIDER;

// Shown until the first ready session lists the organization's aliases.
const FALLBACK_MODELS: Model[] = [
  { id: "mc-standard", name: "Standard", provider: "marketcanopy" },
];

const readJson = (storage: Storage, key: string): unknown => {
  const raw = storage.getItem(key);
  if (!raw) return null;
  try {
    return JSON.parse(raw);
  } catch {
    return null;
  }
};

const sessionModels = (session: MarketCanopySession): Model[] =>
  session.state === "ready"
    ? session.models.map((model) => ({
        id: model.id,
        name: model.name,
        provider: "marketcanopy" as const,
        reasoning: model.capabilities.includes("reasoning"),
      }))
    : [];

/** The models to offer: the session's, else the last ones seen (no secret). */
export const knownModels = (
  storage: Storage,
  session: MarketCanopySession
): Model[] => {
  const fromSession = sessionModels(session);
  if (fromSession.length) {
    storage.setItem(LAST_MODELS_KEY, JSON.stringify(fromSession));
    return fromSession;
  }
  const last = readJson(storage, LAST_MODELS_KEY);
  return Array.isArray(last) && last.length
    ? (last as Model[])
    : FALLBACK_MODELS;
};

/** Keeps the selected model when it is one of the aliases, else the default. */
export const chooseModel = (
  storage: Storage,
  session: MarketCanopySession,
  models: Model[]
): Model => {
  const saved = readJson(storage, CURRENT_MODEL_KEY) as Model | null;
  const kept = saved && models.find((model) => model.id === saved.id);
  if (kept) return kept;
  const preferred =
    session.state === "ready"
      ? models.find((model) => model.id === session.defaultModel)
      : undefined;
  return preferred ?? models[0];
};

const isManaged = (value: unknown): boolean =>
  !!value &&
  typeof value === "object" &&
  (value as TProvider).type === "marketcanopy";

/** The person's own providers, as stored; never the managed one. */
const ownProviders = (storage: Storage): TProvider[] => {
  const stored = readJson(storage, PROVIDERS_KEY);
  if (!Array.isArray(stored)) return [];
  return stored.filter(
    (item): item is TProvider =>
      !!item &&
      typeof item === "object" &&
      typeof (item as TProvider).name === "string" &&
      !isManaged(item) &&
      // The list is keyed by name, and this name is the managed provider's.
      (item as TProvider).name.toLowerCase() !== PROVIDER.name.toLowerCase()
  );
};

/** Applies the managed settings to `storage`; idempotent. */
export const applyManagedSettings = (
  storage: Storage,
  session: MarketCanopySession
): void => {
  const own = ownProviders(storage);
  storage.setItem(PROVIDERS_KEY, JSON.stringify([PROVIDER, ...own]));

  const current = readJson(storage, CURRENT_PROVIDER_KEY);
  const kept =
    current && !isManaged(current)
      ? own.find((item) => item.name === (current as TProvider).name)
      : undefined;
  storage.setItem(CURRENT_PROVIDER_KEY, JSON.stringify(kept ?? PROVIDER));

  // Always read, so the last aliases are remembered for an offline start.
  const models = knownModels(storage, session);
  if (!kept) {
    storage.setItem(
      CURRENT_MODEL_KEY,
      JSON.stringify(chooseModel(storage, session, models))
    );
  }

  const servers = readJson(storage, MCP_SERVERS_KEY);
  if (servers && typeof servers === "object") {
    storage.setItem(
      MCP_SERVERS_KEY,
      JSON.stringify(withoutStdioServers(servers as Record<string, unknown>))
    );
  }
};

const run = () => {
  if (typeof window === "undefined" || !window.localStorage) return;
  try {
    applyManagedSettings(window.localStorage, readSession());
  } catch (error) {
    console.error("Market Canopy AI settings:", error);
  }
};

run();
