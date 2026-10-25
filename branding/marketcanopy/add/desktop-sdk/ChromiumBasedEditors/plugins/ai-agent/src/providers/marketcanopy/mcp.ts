/*
 * Market Canopy Office: no MCP server that runs a command.
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

// ADR 0525 decision 4: stdio MCP servers (a command line run on the person's
// machine) are disabled; HTTP servers (a "url") stay as upstream offers them.
// Both the startup scrub and the settings dialog's save drop stdio entries.

type ServersConfig = { mcpServers?: Record<string, unknown> };

export const isStdioServer = (config: unknown): boolean =>
  !config ||
  typeof config !== "object" ||
  typeof (config as Record<string, unknown>).url !== "string";

/** The configuration with every stdio server removed. */
export const withoutStdioServers = <T extends ServersConfig>(config: T): T => {
  if (!config.mcpServers || typeof config.mcpServers !== "object") {
    return config;
  }
  return {
    ...config,
    mcpServers: Object.fromEntries(
      Object.entries(config.mcpServers).filter(
        ([, server]) => !isStdioServer(server)
      )
    ),
  };
};
