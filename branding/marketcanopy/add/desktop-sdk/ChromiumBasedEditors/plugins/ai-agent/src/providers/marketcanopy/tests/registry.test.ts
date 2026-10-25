/*
 * Market Canopy Office: tests of the provider registry.
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
import { marketcanopyProvider } from "../../marketcanopy";
import {
  getProvider,
  getSupportedProviderTypes,
  isValidProviderType,
  providerRegistry,
} from "../../registry";

describe("registry", () => {
  it("offers the managed provider only", () => {
    expect(getSupportedProviderTypes()).toEqual(["marketcanopy"]);
    expect(Object.keys(providerRegistry)).toEqual(["marketcanopy"]);
    expect(isValidProviderType("marketcanopy")).toBe(true);
    expect(isValidProviderType("openai")).toBe(false);
  });

  it("resolves a stored provider of any type to the managed one", () => {
    expect(getProvider("marketcanopy")).toBe(marketcanopyProvider);
    expect(getProvider("openai")).toBe(marketcanopyProvider);
    expect(getProvider("ollama")).toBe(marketcanopyProvider);
  });
});
