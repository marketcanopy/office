// Market Canopy Office: tests of the in-editor AI plugin's managed provider
// beside upstream's own providers.
// Copyright (C) 2026 Market Canopy LLC
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License version 3 as published by
// the Free Software Foundation, together with the additional terms in the
// LICENSE file at the root of this repository.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// AGPL at https://www.gnu.org/licenses/agpl-3.0.html.
//
// SPDX-License-Identifier: AGPL-3.0-only
//
// Loads the plugin's own engine (storage, providers, engine) from a patched
// onlyoffice.github.io checkout into a fresh context per test, with the
// provider file from the branding overlay, and drives it the way code.js
// does. Run after `build/office-build --check-patches`:
//
//   AI_PLUGIN_DIR=<work>/onlyoffice.github.io/sdkjs-plugins/content/ai \
//     node --test tests/ai-plugin

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import vm from "node:vm";

const PLUGIN_DIR = process.env.AI_PLUGIN_DIR;
if (!PLUGIN_DIR) throw new Error("set AI_PLUGIN_DIR to the plugin folder (sdkjs-plugins/content/ai)");

const ENGINE = ["storage.js", "local_storage.js", "providers/base.js", "providers/provider.js", "engine.js"];
const STORAGE_KEY = "onlyoffice_ai_plugin_storage_key";
const ACTIONS_KEY = "onlyoffice_ai_actions_key";
const TOKEN = "mc-office-ai-SYNTHETIC-MARKER-0001";
const BASE_URL = "https://api.marketcanopy.com/v1/office/ai";
const ORGANIZATION = "66f0c0ffee0000000000b001";

// Values from the plugin's own context are compared as plain data.
const plain = (value) => JSON.parse(JSON.stringify(value));

const ready = (overrides = {}) => ({
  state: "ready",
  baseUrl: BASE_URL,
  accessToken: TOKEN,
  expiresAt: "2026-10-25T12:00:00Z",
  organizationId: ORGANIZATION,
  models: [
    { id: "mc-standard", name: "Canopy", capabilities: ["chat", "tools"] },
    { id: "mc-reasoning", name: "Canopy Think", capabilities: ["chat", "tools", "reasoning"] },
  ],
  defaultModel: "mc-standard",
  ...overrides,
});

/** A plugin page: its storage persists across `boot` calls on one profile. */
function profile(initial = {}) {
  const items = new Map(Object.entries(initial));
  return {
    items,
    dump: () => [...items.entries()].map(([k, v]) => `${k}=${v}`).join("\n"),
    get: (key) => (items.has(key) ? JSON.parse(items.get(key)) : null),
  };
}

async function boot(storage, options = {}) {
  const state = { session: options.session ?? ready(), intervals: [], requests: [] };
  const localStorage = {
    getItem: (k) => (storage.items.has(k) ? storage.items.get(k) : null),
    setItem: (k, v) => storage.items.set(k, String(v)),
    removeItem: (k) => storage.items.delete(k),
  };
  const fetch =
    options.fetch ??
    (async (url, init) => {
      state.requests.push({ url, init });
      return new Response(JSON.stringify({ choices: [{ message: { role: "assistant", content: "Bonjour" } }] }), {
        status: 200,
        headers: { "content-type": "application/json" },
      });
    });
  const context = {
    console,
    setTimeout,
    clearTimeout,
    AbortController,
    TextDecoder,
    Response,
    navigator: { userAgent: "AscDesktopEditor" },
    localStorage,
    fetch,
    setInterval: (fn, ms) => state.intervals.push({ fn, ms }),
    Asc: {
      plugin: { tr: (s) => s },
      // The tokenizer only sizes the input; a word count is enough here.
      OpenAIEncode: (text) => String(text).split(/\s+/),
      Editor: { callMethod: async () => undefined }, Library: { SendError: async () => undefined },
    },
    AscDesktopEditor:
      options.native === false
        ? undefined
        : { getMarketCanopyAiSession: () => JSON.stringify(state.session) },
  };
  context.window = context;
  vm.createContext(context);
  for (const file of ENGINE) {
    const source = fs.readFileSync(path.join(PLUGIN_DIR, "scripts", "engine", file), "utf8");
    vm.runInContext(source, context, { filename: file });
  }
  const AI = context.AI;
  AI.loadResourceAsText = async (relative) => fs.readFileSync(path.join(PLUGIN_DIR, relative), "utf8");
  await AI.loadInternalProviders();
  // code.js registers the actions next (register.js registerButtons) and reads
  // their models from storage; the action list below is register.js's.
  if (options.registerActions !== false) registerActions(AI, storage);
  return { AI, state, context };
}

function registerActions(AI, storage) {
  const types = {
    Chat: AI.CapabilitiesUI.Chat,
    Summarization: AI.CapabilitiesUI.Chat,
    Translation: AI.CapabilitiesUI.Chat,
    TextAnalyze: AI.CapabilitiesUI.Chat,
    ImageGeneration: AI.CapabilitiesUI.Image,
    OCR: AI.CapabilitiesUI.Vision,
    Vision: AI.CapabilitiesUI.Vision,
  };
  AI.Actions = Object.create(null);
  for (const [type, capabilities] of Object.entries(types)) AI.Actions[type] = { name: type, icon: "", model: "", capabilities };
  AI.ActionsSave = () => storage.items.set(ACTIONS_KEY, JSON.stringify(AI.Actions));
  const saved = storage.get(ACTIONS_KEY) || {};
  for (const type in saved) if (AI.Actions[type] && saved[type].model) AI.Actions[type].model = saved[type].model;
}

const upstreamProfile = () =>
  profile({
    [STORAGE_KEY]: JSON.stringify({
      version: 4,
      providers: {
        OpenAI: { name: "OpenAI", url: "https://api.openai.com/v1", key: "sk-upstream-SYNTHETIC", models: [{ id: "gpt-4o", name: "gpt-4o", endpoints: [1], options: {} }] },
        Mine: { name: "Mine", url: "https://llm.example.com", key: "sk-custom-SYNTHETIC", models: [] },
      },
      models: [
        { name: "gpt-4o", id: "gpt-4o", provider: "OpenAI", capabilities: 1 },
        { name: "mine", id: "mine", provider: "Mine", capabilities: 1 },
      ],
      customProviders: { Mine: 'class Provider extends AI.Provider { constructor() { super("Mine", "https://llm.example.com", "sk-custom-SYNTHETIC", "v1"); } }' },
    }),
    [ACTIONS_KEY]: JSON.stringify({ Chat: { model: "gpt-4o" }, Summarization: { model: "mine" }, Vision: { model: "gpt-4o" } }),
  });

test("a clean profile lists Market Canopy first, with the session's aliases", async () => {
  const storage = profile();
  const { AI } = await boot(storage);
  const providers = plain(AI.serializeProviders().map((p) => p.name));
  assert.equal(providers[0], "Market Canopy");
  for (const name of ["OpenAI", "Anthropic", "Ollama"]) assert.ok(providers.includes(name), name);
  assert.deepEqual(
    plain(AI.Models.map((m) => [m.id, m.provider])),
    [["mc-standard", "Market Canopy"], ["mc-reasoning", "Market Canopy"]],
  );
  assert.equal(AI.Actions.Chat.model, "mc-standard");
  assert.equal(AI.Actions.Translation.model, "mc-standard");
  assert.equal(AI.Actions.ImageGeneration.model, "");
  assert.equal(AI.Actions.Vision.model, "", "no alias declares vision");
  assert.doesNotMatch(storage.dump(), new RegExp(TOKEN));
});

test("a vision alias serves the OCR and Vision actions", async () => {
  const session = ready({
    models: [
      { id: "mc-standard", name: "Canopy", capabilities: ["chat", "tools", "vision"] },
    ],
  });
  const { AI } = await boot(profile(), { session });
  assert.equal(AI.Actions.OCR.model, "mc-standard");
  assert.equal(AI.Actions.Vision.model, "mc-standard");
});

test("an upgraded profile keeps the person's providers and loses custom providers", async () => {
  const storage = upstreamProfile();
  const { AI } = await boot(storage);
  assert.equal(AI.Providers.OpenAI.key, "sk-upstream-SYNTHETIC");
  assert.equal(AI.Providers.Mine, undefined);
  assert.deepEqual(
    plain(AI.Models.map((m) => [m.id, m.provider])),
    [["mc-standard", "Market Canopy"], ["mc-reasoning", "Market Canopy"], ["gpt-4o", "OpenAI"]],
  );
  assert.equal(AI.Actions.Chat.model, "gpt-4o", "the person's own model stays chosen");
  assert.equal(AI.Actions.Summarization.model, "mc-standard", "a custom provider's model is replaced");
  assert.equal(AI.Actions.Vision.model, "", "gpt-4o was saved without vision");
  const dump = storage.dump();
  assert.doesNotMatch(dump, /sk-custom|llm\.example\.com/);
  assert.deepEqual(plain(storage.get(STORAGE_KEY).customProviders), {});
});

test("booting again changes nothing", async () => {
  const storage = upstreamProfile();
  await boot(storage);
  const first = storage.dump();
  await boot(storage);
  await boot(storage, { session: ready({ accessToken: "another-SYNTHETIC" }) });
  assert.equal(storage.dump(), first);
});

test("the person's alias choice is kept", async () => {
  const storage = profile();
  const { AI } = await boot(storage);
  AI.Actions.Chat.model = "mc-reasoning";
  AI.ActionsSave();
  const again = await boot(storage);
  assert.equal(again.AI.Actions.Chat.model, "mc-reasoning");
});

test("requests carry the session token and organization to the session's gateway", async () => {
  const { AI, state } = await boot(profile());
  const request = AI.Request.create("Chat");
  const answer = await request.chatRequest("Translate to French: Hello", false);
  assert.equal(answer, "Bonjour");
  assert.equal(state.requests.length, 1);
  const { url, init } = state.requests[0];
  assert.equal(url, `${BASE_URL}/chat/completions`);
  assert.equal(init.headers.Authorization, `Bearer ${TOKEN}`);
  assert.equal(init.headers["x-active-organization-id"], ORGANIZATION);
  assert.equal(init.credentials, "omit");
  assert.equal(JSON.parse(init.body).model, "mc-standard");
});

test("the token is read again for every request", async () => {
  const { AI, state } = await boot(profile());
  await AI.Request.create("Chat").chatRequest("one", false);
  state.session = ready({ accessToken: "mc-office-ai-SYNTHETIC-MARKER-0002" });
  await AI.Request.create("Chat").chatRequest("two", false);
  assert.equal(state.requests[1].init.headers.Authorization, "Bearer mc-office-ai-SYNTHETIC-MARKER-0002");
});

test("nothing is sent while signed out, and the person is told to sign in", async () => {
  const storage = profile();
  const { AI, state, context } = await boot(storage, { session: { state: "signed_out" } });
  const errors = [];
  const request = AI.Request.create("Chat");
  request.setErrorHandler((error) => errors.push(error));
  assert.equal(await request.chatRequest("Hello", false), undefined);
  assert.deepEqual(plain(errors), [
    { error: 1, message: "Sign in to Market Canopy on the Home screen to use Market Canopy AI." },
  ]);
  assert.equal(state.requests.length, 0);
  assert.equal(context.MarketCanopyAI.readSession().state, "signed_out");
  assert.deepEqual(plain(AI.Models.map((m) => m.id)), ["mc-standard"]);
});

test("outside the bundled page the plugin reads as signed out", async () => {
  const { context } = await boot(profile(), { native: false });
  assert.equal(context.MarketCanopyAI.readSession().state, "signed_out");
});

test("a custom or external provider is refused", async () => {
  const storage = profile();
  const { AI } = await boot(storage);
  assert.equal(AI.addCustomProvider('class Provider extends AI.Provider { constructor() { super("X", "https://x.example", "sk-x", "v1"); } }'), false);
  assert.equal(AI.addExternalProvider("", { name: "Y", url: "https://y.example", key: "sk-y" }), false);
  assert.equal(AI.Providers.X, undefined);
  assert.equal(AI.Providers.Y, undefined);
  assert.doesNotMatch(storage.dump(), /sk-x|sk-y/);
});

test("the person can add a model of their own provider, and it survives a session change", async () => {
  const storage = profile();
  const { AI, state } = await boot(storage);
  AI.Storage.addModel({ name: "gpt", id: "gpt", provider: { name: "OpenAI", url: "https://api.openai.com/v1", key: "sk-own-SYNTHETIC" }, capabilities: 1 });
  AI.Actions.Chat.model = "gpt";
  AI.ActionsSave();
  state.session = ready({ organizationId: "66f0c0ffee0000000000b002" });
  state.intervals[0].fn();
  assert.equal(AI.Providers.OpenAI.key, "sk-own-SYNTHETIC");
  assert.ok(AI.Models.some((m) => m.id === "gpt" && m.provider === "OpenAI"));
  assert.equal(AI.Actions.Chat.model, "gpt");
  const again = await boot(storage);
  assert.equal(again.AI.Actions.Chat.model, "gpt");
});

test("a key or URL typed for Market Canopy is ignored", async () => {
  const storage = profile();
  const { AI } = await boot(storage);
  AI.Storage.addModel({ name: "Canopy", id: "mc-standard", provider: { name: "Market Canopy", url: "https://evil.example", key: "sk-typed" }, capabilities: 1 });
  const provider = AI.Providers["Market Canopy"];
  assert.equal(provider.key, "");
  assert.equal(provider.url, BASE_URL);
  assert.doesNotMatch(storage.dump(), /sk-typed|evil\.example/);
});

test("the agent's selected model is not imported", async () => {
  const { AI } = await boot(profile());
  AI.DEFAULT_DESKTOP_MODEL = { id: "[onlyoffice_external]mc-standard" };
  assert.equal(AI.DEFAULT_DESKTOP_MODEL, null);
});

test("an organization change cancels requests in flight and reloads the models", async () => {
  let started;
  const fetch = (url, init) =>
    new Promise((resolve, reject) => {
      started = { url, init };
      // As Chromium's fetch: it rejects with the signal's reason.
      init.signal.addEventListener("abort", () => reject(init.signal.reason));
    });
  const { AI, state, context } = await boot(profile(), { fetch });
  assert.equal(state.intervals.length, 1);
  const errors = [];
  const request = AI.Request.create("Chat");
  request.setErrorHandler((error) => errors.push(error));
  const pending = request.chatRequest("Hello", false);
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.ok(started, "the request started");
  assert.equal(context.MarketCanopyAI.inFlightCount(), 1);

  state.session = ready({
    organizationId: "66f0c0ffee0000000000b002",
    models: [{ id: "mc-reasoning", name: "Canopy Think", capabilities: ["chat"] }],
    defaultModel: "mc-reasoning",
  });
  state.intervals[0].fn();
  await pending;
  assert.equal(context.MarketCanopyAI.inFlightCount(), 0);
  assert.deepEqual(plain(errors), [
    { error: 1, message: "Your Market Canopy account or organization changed. Try again." },
  ]);
  assert.deepEqual(plain(AI.Models.map((m) => m.id)), ["mc-reasoning"]);
  assert.equal(AI.Actions.Chat.model, "mc-reasoning");
});

test("a refreshed token alone changes nothing", async () => {
  const { state, context } = await boot(profile());
  state.session = ready({ accessToken: "mc-office-ai-SYNTHETIC-MARKER-0002" });
  assert.equal(context.MarketCanopyAI.check(), false);
});
