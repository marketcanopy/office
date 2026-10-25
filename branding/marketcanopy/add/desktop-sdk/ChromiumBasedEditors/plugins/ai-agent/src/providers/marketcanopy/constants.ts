/*
 * Market Canopy Office: the managed provider entry.
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

import type { TProvider } from "@/lib/types";

export const MARKETCANOPY_PROVIDER_NAME = "Market Canopy";

/** The provider entry the stores keep: no key, and no URL of its own. */
export const MARKETCANOPY_PROVIDER: TProvider = {
  type: "marketcanopy",
  name: MARKETCANOPY_PROVIDER_NAME,
  baseUrl: "",
};
