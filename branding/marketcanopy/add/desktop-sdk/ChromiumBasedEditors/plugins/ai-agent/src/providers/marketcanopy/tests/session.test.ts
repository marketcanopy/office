/*
 * Market Canopy Office: tests of the session reader.
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

import { afterEach, describe, expect, it, vi } from "vitest";
import { parseSession, readSession, sessionKey } from "../session";
import { BASE_URL, readySession, TOKEN } from "./fakes";

const raw = (overrides: Record<string, unknown> = {}) => ({
  ...readySession(),
  ...overrides,
});

describe("parseSession", () => {
  it("accepts a ready session, as an object or as JSON", () => {
    expect(parseSession(raw())).toEqual(readySession());
    expect(parseSession(JSON.stringify(raw()))).toEqual(readySession());
  });

  it("keeps the unavailable states", () => {
    for (const state of [
      "signed_out",
      "offline",
      "update_required",
      "not_entitled",
    ]) {
      expect(parseSession({ state })).toEqual({ state });
    }
  });

  it("reads anything malformed as offline", () => {
    expect(parseSession("not json")).toEqual({ state: "offline" });
    expect(parseSession(null)).toEqual({ state: "offline" });
    expect(parseSession({ state: "unknown" })).toEqual({ state: "offline" });
    expect(parseSession(raw({ accessToken: "" }))).toEqual({
      state: "offline",
    });
    expect(parseSession(raw({ models: [] }))).toEqual({ state: "offline" });
  });

  it("refuses a base URL that is not an https gateway URL", () => {
    for (const baseUrl of [
      "http://api.marketcanopy.com/v1/office/ai",
      "https://api.openai.com/v1",
      `${BASE_URL}/`,
      "https://user:pass@api.marketcanopy.com/v1/office/ai",
    ]) {
      expect(parseSession(raw({ baseUrl }))).toEqual({ state: "offline" });
    }
  });

  it("drops a model that is not an alias and an unknown capability", () => {
    const session = parseSession(
      raw({
        models: [
          { id: "mc-standard", name: "Standard", capabilities: ["chat", "x"] },
          { id: "GPT 4o", name: "Upstream", capabilities: ["chat"] },
        ],
      })
    );
    expect(session.state === "ready" && session.models).toEqual([
      { id: "mc-standard", name: "Standard", capabilities: ["chat"] },
    ]);
  });

  it("falls back to the first model when the default is not listed", () => {
    const session = parseSession(raw({ defaultModel: "mc-missing" }));
    expect(session.state === "ready" && session.defaultModel).toBe(
      "mc-standard"
    );
  });
});

describe("readSession", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("is signed out outside Office", () => {
    vi.stubGlobal("window", {});
    expect(readSession()).toEqual({ state: "signed_out" });
  });

  it("reads the native method every time", () => {
    const getMarketCanopyAiSession = vi
      .fn()
      .mockReturnValueOnce(JSON.stringify(raw()))
      .mockReturnValueOnce(JSON.stringify({ state: "offline" }));
    vi.stubGlobal("window", {
      AscDesktopEditor: { getMarketCanopyAiSession },
    });
    expect(readSession().state).toBe("ready");
    expect(readSession().state).toBe("offline");
    expect(getMarketCanopyAiSession).toHaveBeenCalledTimes(2);
  });

  it("reads a throwing native method as offline", () => {
    vi.stubGlobal("window", {
      AscDesktopEditor: {
        getMarketCanopyAiSession: () => {
          throw new Error("native");
        },
      },
    });
    expect(readSession()).toEqual({ state: "offline" });
  });
});

describe("sessionKey", () => {
  it("never holds the token", () => {
    expect(sessionKey(readySession())).not.toContain(TOKEN);
  });

  it("changes with the organization, not with the token", () => {
    const key = sessionKey(readySession());
    expect(sessionKey(readySession({ accessToken: "another" }))).toBe(key);
    expect(sessionKey(readySession({ organizationId: "another" }))).not.toBe(
      key
    );
  });
});
