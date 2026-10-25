/*
 * Market Canopy Office: tests of the managed provider.
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

import type { ThreadMessageLike } from "@assistant-ui/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { MarketCanopyProvider } from "../index";
import {
  ACCOUNT_CHANGED_MESSAGE,
  MarketCanopyAccountChangedError,
  MarketCanopyUnavailableError,
  messageForError,
  STATE_MESSAGES,
} from "../messages";
import type { MarketCanopySession } from "../session";
import {
  BASE_URL,
  ORGANIZATION,
  OTHER_TOKEN,
  readySession,
  TOKEN,
} from "./fakes";

type Call = { url: string; init: RequestInit };

let session: MarketCanopySession;
let calls: Call[];
let fetchMock: ReturnType<typeof vi.fn>;

const json = (status: number, body: unknown) =>
  new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json" },
  });

const sse = (chunks: unknown[]) =>
  new Response(
    `${chunks.map((chunk) => `data: ${JSON.stringify(chunk)}\n\n`).join("")}data: [DONE]\n\n`,
    { status: 200, headers: { "content-type": "text/event-stream" } }
  );

const textChunk = (content: string, finish: string | null = null) => ({
  id: "chatcmpl-1",
  object: "chat.completion.chunk",
  created: 1761300000,
  model: "mc-standard",
  choices: [
    {
      index: 0,
      delta: finish ? {} : { role: "assistant", content },
      finish_reason: finish,
    },
  ],
});

const makeProvider = () => {
  const provider = new MarketCanopyProvider();
  provider.setSessionReader(() => session);
  provider.setProvider({
    type: "openai",
    name: "Old",
    key: "sk-old",
    baseUrl: "https://api.openai.com/v1",
  });
  provider.setModelKey("mc-standard");
  return provider;
};

const userMessage: ThreadMessageLike = {
  role: "user",
  content: [{ type: "text", text: "Hello" }],
};

const drain = async (provider: MarketCanopyProvider) => {
  const items = [];
  for await (const item of provider.sendMessage([userMessage])) {
    items.push(item);
  }
  return items;
};

beforeEach(() => {
  session = readySession();
  calls = [];
  fetchMock = vi.fn(async (url: string, init: RequestInit) => {
    calls.push({ url, init });
    return sse([textChunk("Bonjour"), textChunk("", "stop")]);
  });
  vi.stubGlobal("fetch", fetchMock);
  vi.spyOn(console, "error").mockImplementation(() => undefined);
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe("requests", () => {
  it("sends the session token and organization to the session's gateway", async () => {
    const provider = makeProvider();
    const items = await drain(provider);

    expect(items.at(-1)).toMatchObject({ isEnd: true });
    expect(calls).toHaveLength(1);
    expect(calls[0].url).toBe(`${BASE_URL}/chat/completions`);
    const headers = new Headers(calls[0].init.headers);
    expect(headers.get("authorization")).toBe(`Bearer ${TOKEN}`);
    expect(headers.get("x-active-organization-id")).toBe(ORGANIZATION);
    expect(calls[0].init.credentials).toBe("omit");
    const body = JSON.parse(calls[0].init.body as string);
    expect(body.model).toBe("mc-standard");
    expect(body.stream).toBe(true);
    expect(body).not.toHaveProperty("tools");
  });

  it("never uses a stored provider's key or URL", async () => {
    const provider = makeProvider();
    await drain(provider);
    const sent = JSON.stringify(calls[0]);
    expect(sent).not.toContain("sk-old");
    expect(sent).not.toContain("api.openai.com");
    expect(provider.getName()).toBe("Market Canopy");
    expect(provider.getBaseUrl()).toBe(BASE_URL);
  });

  it("reads the token again for every request", async () => {
    const provider = makeProvider();
    await drain(provider);
    session = readySession({ accessToken: OTHER_TOKEN });
    await drain(provider);
    expect(new Headers(calls[1].init.headers).get("authorization")).toBe(
      `Bearer ${OTHER_TOKEN}`
    );
  });

  it("sends nothing and says why when the session is not ready", async () => {
    for (const state of [
      "signed_out",
      "offline",
      "update_required",
      "not_entitled",
    ] as const) {
      session = { state };
      const items = await drain(makeProvider());
      expect(items.at(-1)).toMatchObject({
        isEnd: true,
        responseMessage: { status: { error: STATE_MESSAGES[state] } },
      });
    }
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it("shows the gateway's error code as a sentence", async () => {
    fetchMock.mockImplementation(async () =>
      json(422, {
        error: {
          message: "That request isn't supported.",
          type: "invalid_request_error",
          code: "VALIDATION_ERROR",
          param: "n",
          request_id: "req_example",
        },
      })
    );
    const items = await drain(makeProvider());
    expect(items.at(-1)).toMatchObject({
      responseMessage: {
        status: { error: "AI cannot handle this request. (n)" },
      },
    });
  });

  it("keeps a non-empty tool list", async () => {
    const provider = makeProvider();
    provider.setTools([
      {
        name: "insert_text",
        description: "Insert text",
        inputSchema: { type: "object", properties: {} },
      },
    ] as never);
    await drain(provider);
    expect(JSON.parse(calls[0].init.body as string).tools).toHaveLength(1);
  });
});

describe("cancelAll", () => {
  it("cancels a request in flight with the account-changed message", async () => {
    fetchMock.mockImplementation(
      (_url: string, init: RequestInit) =>
        new Promise((_resolve, reject) => {
          init.signal?.addEventListener("abort", () =>
            reject(init.signal?.reason)
          );
        })
    );
    const provider = makeProvider();
    const pending = provider.sessionFetch(`${BASE_URL}/chat/completions`, {
      method: "POST",
      body: "{}",
    });
    await Promise.resolve();
    expect(provider.inFlightCount()).toBe(1);
    provider.cancelAll();
    await expect(pending).rejects.toBeInstanceOf(
      MarketCanopyAccountChangedError
    );
    expect(provider.inFlightCount()).toBe(0);
  });

  it("releases a request once its body is read", async () => {
    fetchMock.mockImplementation(async () => json(200, { ok: true }));
    const provider = makeProvider();
    const response = await provider.sessionFetch(`${BASE_URL}/models`);
    expect(provider.inFlightCount()).toBe(1);
    await response.text();
    expect(provider.inFlightCount()).toBe(0);
  });
});

describe("models and settings", () => {
  it("lists the session's aliases without a request", async () => {
    const models = await makeProvider().getProviderModels();
    expect(models).toEqual([
      {
        id: "mc-standard",
        name: "Standard",
        provider: "marketcanopy",
        reasoning: false,
      },
      {
        id: "mc-reasoning",
        name: "Reasoning",
        provider: "marketcanopy",
        reasoning: true,
      },
    ]);
    expect(fetchMock).not.toHaveBeenCalled();
  });

  it("lists nothing while signed out", async () => {
    session = { state: "signed_out" };
    expect(await makeProvider().getProviderModels()).toEqual([]);
  });

  it("refuses a provider the person configures", async () => {
    const provider = makeProvider();
    expect(
      await provider.checkProvider({ url: "https://api.openai.com/v1" })
    ).toMatchObject({ field: "url" });
    expect(
      await provider.checkProvider({ url: "", apiKey: "sk" })
    ).toMatchObject({ field: "url" });
    expect(await provider.checkProvider({ url: "" })).toBe(true);
  });
});

describe("messageForError", () => {
  it("maps states, codes and statuses", () => {
    expect(messageForError(new MarketCanopyUnavailableError("offline"))).toBe(
      STATE_MESSAGES.offline
    );
    expect(
      messageForError({ cause: new MarketCanopyAccountChangedError() })
    ).toBe(ACCOUNT_CHANGED_MESSAGE);
    expect(messageForError({ error: { code: "QUOTA_EXCEEDED" } })).toBe(
      "Your organization has used its AI allowance."
    );
    expect(messageForError({ status: 429 })).toBe(
      "Too many AI requests. Wait a minute and try again."
    );
    expect(messageForError(new Error("Bearer secret"))).not.toContain("secret");
  });
});
