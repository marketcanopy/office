/*
 * Market Canopy Office: follow the person's account and organization.
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

// ADR 0525 decision 1: on an account or organization change, requests in
// flight are cancelled and the model list reloads. Native code pushes no
// event to the agent, so the session is read every few seconds; what is
// compared (sessionKey) holds no token.

import useModelsStore from "@/store/useModelsStore";
import useProviders from "@/store/useProviders";
import { chooseModel, knownModels } from "./bootstrap";
import { marketcanopyProvider } from "./index";
import { type MarketCanopySession, readSession, sessionKey } from "./session";

export const WATCH_INTERVAL_MS = 3000;

type Listener = (session: MarketCanopySession) => void;

const listeners = new Set<Listener>();
let lastSession: MarketCanopySession = { state: "signed_out" };
let lastKey = "";

/** The session the watcher saw last (settings page). */
export const currentSession = (): MarketCanopySession => lastSession;

/** Called with every changed session; returns the unsubscribe function. */
export const onSessionChange = (listener: Listener): (() => void) => {
  listeners.add(listener);
  return () => listeners.delete(listener);
};

/**
 * Compares the session with the last one and, when the account,
 * organization, state or aliases changed, cancels requests in flight and
 * reloads the models. Returns whether anything changed.
 */
export const checkSession = (
  read: () => MarketCanopySession = readSession
): boolean => {
  const session = read();
  const key = sessionKey(session);
  lastSession = session;
  if (key === lastKey) return false;
  const first = lastKey === "";
  lastKey = key;
  if (!first) marketcanopyProvider.cancelAll();

  const models = knownModels(window.localStorage, session);
  const selected = useModelsStore.getState().currentModel;
  if (!selected || !models.some((model) => model.id === selected.id)) {
    useModelsStore
      .getState()
      .selectModel(chooseModel(window.localStorage, session, models));
  }
  void useProviders.getState().fetchProvidersModels();
  for (const listener of listeners) listener(session);
  return true;
};

/** Tests start from a clean slate. */
export const resetWatch = (): void => {
  lastKey = "";
  lastSession = { state: "signed_out" };
  listeners.clear();
};

if (typeof window !== "undefined" && window.localStorage) {
  checkSession();
  window.setInterval(() => checkSession(), WATCH_INTERVAL_MS);
}
