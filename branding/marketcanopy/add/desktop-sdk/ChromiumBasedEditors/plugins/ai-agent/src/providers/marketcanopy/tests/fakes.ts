/*
 * Market Canopy Office: test doubles for storage, IndexedDB and the session.
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

import type { MarketCanopySession } from "../session";

/** An in-memory Storage. */
export class MemoryStorage implements Storage {
  private items = new Map<string, string>();

  get length(): number {
    return this.items.size;
  }

  clear(): void {
    this.items.clear();
  }

  getItem(key: string): string | null {
    return this.items.has(key) ? (this.items.get(key) as string) : null;
  }

  key(index: number): string | null {
    return [...this.items.keys()][index] ?? null;
  }

  removeItem(key: string): void {
    this.items.delete(key);
  }

  setItem(key: string, value: string): void {
    this.items.set(key, String(value));
  }

  /** Every stored value, joined, for secret scans. */
  dump(): string {
    return [...this.items.entries()]
      .map(([key, value]) => `${key}=${value}`)
      .join("\n");
  }
}

export const TOKEN = "mc-office-ai-SYNTHETIC-MARKER-0001";
export const OTHER_TOKEN = "mc-office-ai-SYNTHETIC-MARKER-0002";
export const ORGANIZATION = "66f0c0ffee0000000000b001";
export const OTHER_ORGANIZATION = "66f0c0ffee0000000000b002";
export const BASE_URL = "https://api.marketcanopy.com/v1/office/ai";

export const readySession = (
  overrides: Partial<Extract<MarketCanopySession, { state: "ready" }>> = {}
): MarketCanopySession => ({
  state: "ready",
  baseUrl: BASE_URL,
  accessToken: TOKEN,
  expiresAt: "2026-10-25T12:00:00Z",
  organizationId: ORGANIZATION,
  models: [
    { id: "mc-standard", name: "Standard", capabilities: ["chat", "tools"] },
    {
      id: "mc-reasoning",
      name: "Reasoning",
      capabilities: ["chat", "tools", "reasoning"],
    },
  ],
  defaultModel: "mc-standard",
  ...overrides,
});

type Thread = { id: string; provider?: Record<string, unknown> };

/**
 * Just enough of IDBFactory for scrubThreadProviders: one "ChatHistory"
 * database with a "threads" store, or no database at all.
 */
export class FakeIndexedDB {
  threads: Thread[] | null;

  created = false;

  constructor(threads: Thread[] | null) {
    this.threads = threads;
  }

  open(_name: string): IDBOpenDBRequest {
    const request = {} as {
      result: unknown;
      transaction: { abort: () => void } | null;
      onupgradeneeded: (() => void) | null;
      onsuccess: (() => void) | null;
      onerror: (() => void) | null;
    };
    queueMicrotask(() => {
      if (this.threads === null) {
        let aborted = false;
        request.transaction = {
          abort: () => {
            aborted = true;
          },
        };
        request.onupgradeneeded?.();
        if (aborted) {
          request.onerror?.();
          return;
        }
        this.created = true;
        this.threads = [];
      }
      request.result = this.database();
      request.onsuccess?.();
    });
    return request as unknown as IDBOpenDBRequest;
  }

  private database(): IDBDatabase {
    const threads = this.threads as Thread[];
    return {
      objectStoreNames: { contains: (name: string) => name === "threads" },
      close: () => undefined,
      transaction: () => {
        const transaction = {
          oncomplete: null as (() => void) | null,
          onerror: null as (() => void) | null,
          objectStore: () => ({
            openCursor: () => {
              const cursorRequest = {
                result: null as unknown,
                onsuccess: null as (() => void) | null,
              };
              let index = 0;
              const step = () =>
                queueMicrotask(() => {
                  if (index >= threads.length) {
                    cursorRequest.result = null;
                    cursorRequest.onsuccess?.();
                    transaction.oncomplete?.();
                    return;
                  }
                  const position = index;
                  cursorRequest.result = {
                    value: threads[position],
                    update: (value: Thread) => {
                      threads[position] = value;
                    },
                    continue: () => {
                      index += 1;
                      step();
                    },
                  };
                  cursorRequest.onsuccess?.();
                });
              step();
              return cursorRequest;
            },
          }),
        };
        return transaction;
      },
    } as unknown as IDBDatabase;
  }
}
