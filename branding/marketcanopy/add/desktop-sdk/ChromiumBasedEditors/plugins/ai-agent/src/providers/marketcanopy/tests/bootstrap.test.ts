/*
 * Market Canopy Office: tests of the stored-settings bootstrap.
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

import { describe, expect, it } from "vitest";
import {
  applyManagedSettings,
  CURRENT_MODEL_KEY,
  CURRENT_PROVIDER_KEY,
  LAST_MODELS_KEY,
  MCP_SERVERS_KEY,
  PROVIDERS_KEY,
} from "../bootstrap";
import { withoutStdioServers } from "../mcp";
import { MemoryStorage, OTHER_TOKEN, readySession, TOKEN } from "./fakes";

// Upstream's keys, which the bootstrap leaves alone.
const DISABLED_TOOLS_KEY = "disabledTools";
const WEB_SEARCH_DATA_KEY = "webSearchProviderData";

const MANAGED = { type: "marketcanopy", name: "Market Canopy", baseUrl: "" };

const read = (storage: Storage, key: string) =>
  JSON.parse(storage.getItem(key) as string);

describe("applyManagedSettings", () => {
  it("sets up a clean profile with the managed provider and the default model", () => {
    const storage = new MemoryStorage();
    applyManagedSettings(storage, readySession());

    expect(read(storage, PROVIDERS_KEY)).toEqual([MANAGED]);
    expect(read(storage, CURRENT_PROVIDER_KEY)).toEqual(MANAGED);
    expect(read(storage, CURRENT_MODEL_KEY)).toMatchObject({
      id: "mc-standard",
      provider: "marketcanopy",
    });
    expect(storage.getItem(DISABLED_TOOLS_KEY)).toBeNull();
    expect(storage.dump()).not.toContain(TOKEN);
  });

  it("sets up a signed-out profile without models from the session", () => {
    const storage = new MemoryStorage();
    applyManagedSettings(storage, { state: "signed_out" });
    expect(read(storage, PROVIDERS_KEY)).toEqual([MANAGED]);
    expect(read(storage, CURRENT_MODEL_KEY).id).toBe("mc-standard");
  });

  it("gives the same result when it runs again", () => {
    const storage = new MemoryStorage();
    applyManagedSettings(storage, readySession());
    const first = storage.dump();
    applyManagedSettings(storage, readySession());
    applyManagedSettings(storage, readySession({ accessToken: OTHER_TOKEN }));
    expect(storage.dump()).toBe(first);
  });

  it("keeps the person's model choice when it is still offered", () => {
    const storage = new MemoryStorage();
    applyManagedSettings(storage, readySession());
    storage.setItem(
      CURRENT_MODEL_KEY,
      JSON.stringify({ id: "mc-reasoning", name: "Reasoning" })
    );
    applyManagedSettings(storage, readySession());
    expect(read(storage, CURRENT_MODEL_KEY).id).toBe("mc-reasoning");

    applyManagedSettings(
      storage,
      readySession({
        models: [{ id: "mc-standard", name: "Standard", capabilities: [] }],
      })
    );
    expect(read(storage, CURRENT_MODEL_KEY).id).toBe("mc-standard");
  });

  it("remembers the last aliases, without the token, for a later offline start", () => {
    const storage = new MemoryStorage();
    applyManagedSettings(storage, readySession());
    applyManagedSettings(storage, { state: "offline" });
    expect(
      read(storage, LAST_MODELS_KEY).map((m: { id: string }) => m.id)
    ).toEqual(["mc-standard", "mc-reasoning"]);
    expect(storage.dump()).not.toContain(TOKEN);
  });

  it("keeps the person's own providers and their selection", () => {
    const storage = new MemoryStorage();
    const own = {
      type: "openai",
      name: "OpenAI",
      key: "sk-own-SYNTHETIC",
      baseUrl: "https://api.openai.com/v1",
    };
    const model = { id: "gpt-4o", name: "GPT-4o", provider: "openai" };
    storage.setItem(PROVIDERS_KEY, JSON.stringify([own]));
    storage.setItem(CURRENT_PROVIDER_KEY, JSON.stringify(own));
    storage.setItem(CURRENT_MODEL_KEY, JSON.stringify(model));
    storage.setItem(
      MCP_SERVERS_KEY,
      JSON.stringify({
        mcpServers: {
          local: { command: "npx", args: ["server"] },
          remote: { url: "https://mcp.example.com/mcp" },
        },
      })
    );
    storage.setItem(DISABLED_TOOLS_KEY, JSON.stringify({ "web-search": [] }));
    storage.setItem(
      WEB_SEARCH_DATA_KEY,
      JSON.stringify({ provider: "Exa", key: "exa-key-SYNTHETIC" })
    );

    applyManagedSettings(storage, readySession());

    expect(read(storage, PROVIDERS_KEY)).toEqual([MANAGED, own]);
    expect(read(storage, CURRENT_PROVIDER_KEY)).toEqual(own);
    expect(read(storage, CURRENT_MODEL_KEY)).toEqual(model);
    // Web search keeps the person's own Exa key.
    expect(read(storage, WEB_SEARCH_DATA_KEY)).toEqual({
      provider: "Exa",
      key: "exa-key-SYNTHETIC",
    });
    expect(read(storage, MCP_SERVERS_KEY)).toEqual({
      mcpServers: { remote: { url: "https://mcp.example.com/mcp" } },
    });
    // The person's upstream tool switch is kept.
    expect(read(storage, DISABLED_TOOLS_KEY)).toEqual({ "web-search": [] });

    const first = storage.dump();
    applyManagedSettings(storage, readySession());
    expect(storage.dump()).toBe(first);
  });

  it("selects Market Canopy when the selected provider is gone", () => {
    const storage = new MemoryStorage();
    storage.setItem(
      CURRENT_PROVIDER_KEY,
      JSON.stringify({ type: "openai", name: "Deleted", key: "k", baseUrl: "" })
    );
    storage.setItem(
      CURRENT_MODEL_KEY,
      JSON.stringify({ id: "gpt-4o", name: "GPT-4o", provider: "openai" })
    );
    applyManagedSettings(storage, readySession());
    expect(read(storage, CURRENT_PROVIDER_KEY)).toEqual(MANAGED);
    expect(read(storage, CURRENT_MODEL_KEY).id).toBe("mc-standard");
  });

  it("normalizes a stored Market Canopy entry and drops a provider using its name", () => {
    const storage = new MemoryStorage();
    const stale = { ...MANAGED, key: "stale-SYNTHETIC", baseUrl: "https://x" };
    const lookalike = {
      type: "openaicompatible",
      name: "market canopy",
      key: "k",
      baseUrl: "https://example.com/v1",
    };
    storage.setItem(PROVIDERS_KEY, JSON.stringify([stale, lookalike]));
    storage.setItem(CURRENT_PROVIDER_KEY, JSON.stringify(stale));
    applyManagedSettings(storage, readySession());
    expect(read(storage, PROVIDERS_KEY)).toEqual([MANAGED]);
    expect(read(storage, CURRENT_PROVIDER_KEY)).toEqual(MANAGED);
    expect(storage.dump()).not.toContain("stale-SYNTHETIC");
  });
});

describe("withoutStdioServers", () => {
  it("drops every server without a URL", () => {
    expect(
      withoutStdioServers({
        mcpServers: {
          a: { command: "sh", args: ["-c", "id"] },
          b: { url: "https://mcp.example.com" },
          c: null,
        },
      })
    ).toEqual({ mcpServers: { b: { url: "https://mcp.example.com" } } });
  });
});
