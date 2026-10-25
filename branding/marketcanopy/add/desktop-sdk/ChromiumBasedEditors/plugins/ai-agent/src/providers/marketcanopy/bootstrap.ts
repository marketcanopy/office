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
// read localStorage. It makes the managed provider the only and the selected
// one, removes every stored provider key (a profile from an upstream build
// keeps them in "providers", "current-provider" and in each saved thread),
// drops stdio MCP servers (native code no longer starts processes) and
// leaves web search off until the person turns it on.

import type { Model, TProvider } from "@/lib/types";
import { MARKETCANOPY_PROVIDER } from "./constants";
import { withoutStdioServers } from "./mcp";
import { type MarketCanopySession, readSession } from "./session";

export const PROVIDERS_KEY = "providers";
export const CURRENT_PROVIDER_KEY = "current-provider";
export const CURRENT_MODEL_KEY = "current-model";
export const MCP_SERVERS_KEY = "mcpServers";
export const DISABLED_TOOLS_KEY = "disabledTools";
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

/** Applies the managed settings to `storage`; idempotent. */
export const applyManagedSettings = (
  storage: Storage,
  session: MarketCanopySession
): void => {
  storage.setItem(PROVIDERS_KEY, JSON.stringify([PROVIDER]));
  storage.setItem(CURRENT_PROVIDER_KEY, JSON.stringify(PROVIDER));

  const models = knownModels(storage, session);
  storage.setItem(
    CURRENT_MODEL_KEY,
    JSON.stringify(chooseModel(storage, session, models))
  );

  const servers = readJson(storage, MCP_SERVERS_KEY);
  if (servers && typeof servers === "object") {
    storage.setItem(
      MCP_SERVERS_KEY,
      JSON.stringify(withoutStdioServers(servers as Record<string, unknown>))
    );
  }

  if (!storage.getItem(DISABLED_TOOLS_KEY)) {
    // useServersStore reads a non-empty "web-search" list as "off".
    storage.setItem(
      DISABLED_TOOLS_KEY,
      JSON.stringify({ "web-search": ["web_search"] })
    );
  }
};

/**
 * Rewrites every saved thread's provider to the managed one, so a thread
 * from an upstream build neither keeps nor uses a provider key.
 */
export const scrubThreadProviders = (factory: IDBFactory): Promise<number> =>
  new Promise((resolve) => {
    let request: IDBOpenDBRequest;
    try {
      request = factory.open("ChatHistory");
    } catch {
      resolve(0);
      return;
    }
    request.onupgradeneeded = () => {
      // No history yet: leave creating the stores to the agent.
      request.transaction?.abort();
    };
    request.onerror = () => resolve(0);
    request.onsuccess = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains("threads")) {
        db.close();
        resolve(0);
        return;
      }
      let changed = 0;
      const transaction = db.transaction("threads", "readwrite");
      const store = transaction.objectStore("threads");
      const cursorRequest = store.openCursor();
      cursorRequest.onsuccess = () => {
        const cursor = cursorRequest.result;
        if (!cursor) return;
        const thread = cursor.value as { provider?: TProvider };
        if (
          thread.provider &&
          (thread.provider.type !== "marketcanopy" ||
            thread.provider.key ||
            thread.provider.baseUrl)
        ) {
          cursor.update({ ...thread, provider: PROVIDER });
          changed += 1;
        }
        cursor.continue();
      };
      transaction.oncomplete = () => {
        db.close();
        resolve(changed);
      };
      transaction.onerror = () => {
        db.close();
        resolve(changed);
      };
    };
  });

const run = () => {
  if (typeof window === "undefined" || !window.localStorage) return;
  try {
    applyManagedSettings(window.localStorage, readSession());
  } catch (error) {
    console.error("Market Canopy AI settings:", error);
  }
  if (window.indexedDB) void scrubThreadProviders(window.indexedDB);
};

run();
