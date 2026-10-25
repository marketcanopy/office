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
  DISABLED_TOOLS_KEY,
  LAST_MODELS_KEY,
  MCP_SERVERS_KEY,
  PROVIDERS_KEY,
  scrubThreadProviders,
  WEB_SEARCH_DATA_KEY,
} from "../bootstrap";
import { withoutStdioServers } from "../mcp";
import {
  FakeIndexedDB,
  MemoryStorage,
  OTHER_TOKEN,
  readySession,
  TOKEN,
} from "./fakes";

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
    expect(read(storage, DISABLED_TOOLS_KEY)).toEqual({
      "web-search": ["web_search"],
    });
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

  it("upgrades a profile from an upstream build", () => {
    const storage = new MemoryStorage();
    const old = {
      type: "openai",
      name: "OpenAI",
      key: "sk-upstream-SYNTHETIC",
      baseUrl: "https://api.openai.com/v1",
    };
    storage.setItem(PROVIDERS_KEY, JSON.stringify([old]));
    storage.setItem(CURRENT_PROVIDER_KEY, JSON.stringify(old));
    storage.setItem(
      CURRENT_MODEL_KEY,
      JSON.stringify({ id: "gpt-4o", name: "GPT-4o", provider: "openai" })
    );
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

    expect(storage.dump()).not.toContain("sk-upstream-SYNTHETIC");
    expect(storage.dump()).not.toContain("api.openai.com");
    expect(storage.dump()).not.toContain("exa-key-SYNTHETIC");
    expect(storage.getItem(WEB_SEARCH_DATA_KEY)).toBeNull();
    expect(read(storage, PROVIDERS_KEY)).toEqual([MANAGED]);
    expect(read(storage, CURRENT_MODEL_KEY).id).toBe("mc-standard");
    expect(read(storage, MCP_SERVERS_KEY)).toEqual({
      mcpServers: { remote: { url: "https://mcp.example.com/mcp" } },
    });
    // The person's upstream tool switch is kept; with no search key there
    // is no web search tool to switch on.
    expect(read(storage, DISABLED_TOOLS_KEY)).toEqual({ "web-search": [] });
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

describe("scrubThreadProviders", () => {
  it("rewrites every saved thread's provider and keeps the rest", async () => {
    const factory = new FakeIndexedDB([
      {
        id: "1",
        provider: {
          type: "anthropic",
          name: "Claude",
          key: "sk-thread-SYNTHETIC",
          baseUrl: "https://api.anthropic.com",
        },
      },
      { id: "2", provider: { ...MANAGED } },
      { id: "3" },
    ]);

    expect(await scrubThreadProviders(factory as unknown as IDBFactory)).toBe(
      1
    );
    expect(factory.threads).toEqual([
      { id: "1", provider: MANAGED },
      { id: "2", provider: MANAGED },
      { id: "3" },
    ]);
    expect(JSON.stringify(factory.threads)).not.toContain("sk-thread");

    expect(await scrubThreadProviders(factory as unknown as IDBFactory)).toBe(
      0
    );
  });

  it("creates no database on a clean profile", async () => {
    const factory = new FakeIndexedDB(null);
    expect(await scrubThreadProviders(factory as unknown as IDBFactory)).toBe(
      0
    );
    expect(factory.created).toBe(false);
  });
});
