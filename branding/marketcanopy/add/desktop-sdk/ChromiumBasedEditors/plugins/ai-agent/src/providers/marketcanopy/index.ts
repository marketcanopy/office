/*
 * Market Canopy Office: the managed "marketcanopy" provider of the AI agent.
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

// ADR 0525: Market Canopy is Office's only AI provider. The provider speaks
// the OpenAI wire format to the Office AI gateway; the base URL, the model
// aliases and a short-lived token come from getMarketCanopyAiSession() for
// every request, so nothing secret is ever stored and the person types no
// URL or key. A stored provider of another type (an older profile, a thread
// saved before the upgrade) resolves to this one and its key and URL are
// ignored.

import type { ThreadMessageLike } from "@assistant-ui/react";
import OpenAI from "openai";
import type { Model, TProvider } from "@/lib/types";
import type { TData, TErrorData } from "../base";
import { OpenAICompatibleProvider } from "../openaicompatible";
import { MARKETCANOPY_PROVIDER, MARKETCANOPY_PROVIDER_NAME } from "./constants";
import {
  MarketCanopyAccountChangedError,
  MarketCanopyUnavailableError,
  messageForError,
  OTHER_PROVIDER_MESSAGE,
} from "./messages";
import { type MarketCanopySession, readSession } from "./session";

// The OpenAI SDK refuses to build a client without a key. This placeholder
// is not a credential: the request's Authorization header is replaced with
// the session token on the way out.
const PLACEHOLDER_KEY = "market-canopy-session";

type SessionReader = () => MarketCanopySession;

const toModel = (model: {
  id: string;
  name: string;
  capabilities: string[];
}): Model => ({
  id: model.id,
  name: model.name,
  provider: "marketcanopy",
  reasoning: model.capabilities.includes("reasoning"),
});

class MarketCanopyProvider extends OpenAICompatibleProvider {
  private readSession: SessionReader = readSession;

  private inFlight = new Set<AbortController>();

  /** Tests replace the native reader. */
  setSessionReader = (reader: SessionReader): void => {
    this.readSession = reader;
  };

  /**
   * fetch for the OpenAI client: reads the session for this request, refuses
   * to send anything when it is not ready, and sets the token, the
   * organization and the session's base URL. A request can be cancelled by
   * an account or organization change (cancelAll).
   */
  sessionFetch = async (
    input: string | URL | Request,
    init?: RequestInit
  ): Promise<Response> => {
    const session = this.readSession();
    if (session.state !== "ready") {
      throw new MarketCanopyUnavailableError(session.state);
    }

    const requested = new URL(
      typeof input === "string"
        ? input
        : input instanceof URL
          ? input.href
          : input.url
    );
    const base = new URL(session.baseUrl);
    // Only the gateway's own paths, always on the session's origin.
    const path = requested.pathname.startsWith(base.pathname)
      ? requested.pathname
      : `${base.pathname}${requested.pathname}`;
    const url = `${base.origin}${path}${requested.search}`;

    const headers = new Headers(init?.headers);
    headers.set("Authorization", `Bearer ${session.accessToken}`);
    headers.set("x-active-organization-id", session.organizationId);

    let body = init?.body;
    if (typeof body === "string" && body.includes('"tools":[]')) {
      // The agent sends an empty tool list when every tool is off; the
      // gateway's providers do not all accept one.
      try {
        const { tools, ...rest } = JSON.parse(body) as Record<string, unknown>;
        if (Array.isArray(tools) && tools.length === 0) {
          body = JSON.stringify(rest);
        }
      } catch {
        // not JSON: send as is
      }
    }

    const controller = new AbortController();
    const upstreamSignal = init?.signal;
    const onAbort = () => controller.abort(upstreamSignal?.reason);
    if (upstreamSignal) {
      if (upstreamSignal.aborted) controller.abort(upstreamSignal.reason);
      else upstreamSignal.addEventListener("abort", onAbort, { once: true });
    }
    this.inFlight.add(controller);

    try {
      const response = await fetch(url, {
        ...init,
        body,
        headers,
        signal: controller.signal,
        credentials: "omit",
      });
      // A streamed body outlives this call; release it when the body ends.
      if (response.body) {
        const reader = response.body.getReader();
        const release = () => this.inFlight.delete(controller);
        const stream = new ReadableStream<Uint8Array>({
          pull: async (streamController) => {
            try {
              const { done, value } = await reader.read();
              if (done) {
                release();
                streamController.close();
              } else {
                streamController.enqueue(value);
              }
            } catch (error) {
              release();
              streamController.error(error);
            }
          },
          cancel: (reason) => {
            release();
            return reader.cancel(reason);
          },
        });
        return new Response(stream, {
          status: response.status,
          statusText: response.statusText,
          headers: response.headers,
        });
      }
      this.inFlight.delete(controller);
      return response;
    } catch (error) {
      this.inFlight.delete(controller);
      if (controller.signal.reason instanceof MarketCanopyAccountChangedError) {
        throw controller.signal.reason;
      }
      throw error;
    } finally {
      upstreamSignal?.removeEventListener("abort", onAbort);
    }
  };

  /** Cancels every request in flight: the account or organization changed. */
  cancelAll = (): void => {
    for (const controller of this.inFlight) {
      controller.abort(new MarketCanopyAccountChangedError());
    }
    this.inFlight.clear();
  };

  /** Requests in flight (tests). */
  inFlightCount = (): number => this.inFlight.size;

  protected createClient(): OpenAI {
    const session = this.readSession();
    return new OpenAI({
      apiKey: PLACEHOLDER_KEY,
      baseURL:
        session.state === "ready"
          ? session.baseUrl
          : "https://api.marketcanopy.com/v1/office/ai",
      dangerouslyAllowBrowser: true,
      // A retry is a second metered request; the gateway already falls back
      // between models.
      maxRetries: 0,
      fetch: this.sessionFetch,
    });
  }

  // A stored provider's key and URL are never used.
  setProvider = (_provider: TProvider): void => {
    this.provider = MARKETCANOPY_PROVIDER;
    this.client = this.createClient();
  };

  setApiKey(_apiKey: string): void {
    // The token comes from the session for every request.
  }

  setUrl(_url: string): void {
    // The base URL comes from the session for every request.
  }

  getName = (): string => MARKETCANOPY_PROVIDER_NAME;

  getBaseUrl = (): string => {
    const session = this.readSession();
    return session.state === "ready" ? session.baseUrl : "";
  };

  /** The session's aliases; no request is made. */
  getProviderModels = async (_data?: TData): Promise<Model[]> => {
    const session = this.readSession();
    return session.state === "ready" ? session.models.map(toModel) : [];
  };

  checkProvider = async (data: TData): Promise<boolean | TErrorData> => {
    // A provider the person configures (a URL or a key) is never accepted.
    if (data.url || data.apiKey) {
      return { field: "url", message: OTHER_PROVIDER_MESSAGE };
    }
    const session = this.readSession();
    return session.state === "ready"
      ? true
      : {
          field: "url",
          message: messageForError(
            new MarketCanopyUnavailableError(session.state)
          ),
        };
  };

  async *sendMessage(
    messages: ThreadMessageLike[],
    afterToolCall?: boolean,
    previousMessage?: ThreadMessageLike,
    withThinking?: boolean
  ): AsyncGenerator<
    ThreadMessageLike | { isEnd: true; responseMessage: ThreadMessageLike }
  > {
    if (!this.client) this.client = this.createClient();
    for await (const item of super.sendMessage(
      messages,
      afterToolCall,
      previousMessage,
      withThinking
    )) {
      if (
        "isEnd" in item &&
        item.responseMessage.status?.type === "incomplete"
      ) {
        const status = item.responseMessage.status;
        if (status.reason === "error") {
          yield {
            isEnd: true,
            responseMessage: {
              ...item.responseMessage,
              status: { ...status, error: messageForError(status.error) },
            },
          };
          continue;
        }
      }
      yield item;
    }
  }
}

const marketcanopyProvider = new MarketCanopyProvider();

export { MarketCanopyProvider, marketcanopyProvider };
