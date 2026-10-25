/*
 * Market Canopy Office: tests of the account and organization watcher.
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

import { beforeEach, describe, expect, it, vi } from "vitest";

// The stores read localStorage when they are created, and the watcher reads
// window.localStorage, so both exist before the modules load.
vi.hoisted(() => {
  const items = new Map<string, string>();
  const storage = {
    getItem: (key: string) => items.get(key) ?? null,
    setItem: (key: string, value: string) => items.set(key, String(value)),
    removeItem: (key: string) => items.delete(key),
    clear: () => items.clear(),
  };
  Object.assign(globalThis, {
    localStorage: storage,
    window: { localStorage: storage, setInterval: () => 0 },
  });
});

import useModelsStore from "@/store/useModelsStore";
import useProviders from "@/store/useProviders";
import { marketcanopyProvider } from "../index";
import type { MarketCanopySession } from "../session";
import { checkSession, onSessionChange, resetWatch } from "../watch";
import { OTHER_ORGANIZATION, OTHER_TOKEN, readySession } from "./fakes";

let session: MarketCanopySession;
const read = () => session;

beforeEach(() => {
  resetWatch();
  localStorage.clear();
  session = readySession();
  vi.restoreAllMocks();
});

describe("checkSession", () => {
  it("does not cancel anything on the first read", () => {
    const cancel = vi.spyOn(marketcanopyProvider, "cancelAll");
    expect(checkSession(read)).toBe(true);
    expect(cancel).not.toHaveBeenCalled();
    expect(useModelsStore.getState().currentModel?.id).toBe("mc-standard");
  });

  it("ignores a refreshed token", () => {
    checkSession(read);
    const cancel = vi.spyOn(marketcanopyProvider, "cancelAll");
    session = readySession({ accessToken: OTHER_TOKEN });
    expect(checkSession(read)).toBe(false);
    expect(cancel).not.toHaveBeenCalled();
  });

  it("cancels requests and reloads the models when the organization changes", () => {
    checkSession(read);
    const cancel = vi.spyOn(marketcanopyProvider, "cancelAll");
    const reload = vi.spyOn(useProviders.getState(), "fetchProvidersModels");
    const seen: MarketCanopySession[] = [];
    onSessionChange((next) => seen.push(next));

    session = readySession({
      organizationId: OTHER_ORGANIZATION,
      models: [{ id: "mc-reasoning", name: "Reasoning", capabilities: [] }],
      defaultModel: "mc-reasoning",
    });
    expect(checkSession(read)).toBe(true);
    expect(cancel).toHaveBeenCalledTimes(1);
    expect(reload).toHaveBeenCalledTimes(1);
    expect(useModelsStore.getState().currentModel?.id).toBe("mc-reasoning");
    expect(seen).toEqual([session]);
  });

  it("cancels requests on sign-out", () => {
    checkSession(read);
    const cancel = vi.spyOn(marketcanopyProvider, "cancelAll");
    session = { state: "signed_out" };
    expect(checkSession(read)).toBe(true);
    expect(cancel).toHaveBeenCalledTimes(1);
  });
});
