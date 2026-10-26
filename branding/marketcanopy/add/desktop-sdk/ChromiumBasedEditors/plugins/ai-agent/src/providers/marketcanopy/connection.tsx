/*
 * Market Canopy Office: the Connection settings panel.
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

// Shown above the person's own providers on the Connection tab: Market
// Canopy is set up by the bootstrap, so there is nothing to add, edit or
// delete, and no URL or key to type. The panel says whether AI is ready and
// lists the aliases.

import React from "react";
import { useTranslation } from "react-i18next";
import { useDirection } from "@/hooks/useDirection";
import { cn } from "@/lib/utils";
import { STATE_MESSAGES } from "./messages";
import type { MarketCanopySession } from "./session";
import { currentSession, onSessionChange } from "./watch";

type MarketCanopyConnectionProps = {
  isActive?: boolean;
};

const MarketCanopyConnection = (_props: MarketCanopyConnectionProps) => {
  const { t } = useTranslation();
  const { isRTL } = useDirection();
  const [session, setSession] =
    React.useState<MarketCanopySession>(currentSession);

  React.useEffect(() => {
    setSession(currentSession());
    return onSessionChange(setSession);
  }, []);

  return (
    <div
      className={cn("flex flex-col gap-[8px]", isRTL ? "items-end" : "")}
      data-testid="marketcanopy-connection"
    >
      <p className="font-normal text-[14px] leading-[20px] text-[var(--text-normal)]">
        {t(
          "MarketCanopyAIProvider",
          "AI in Market Canopy Office is provided by Market Canopy, through your organization."
        )}
      </p>
      {session.state === "ready" ? (
        <>
          <p className="text-[14px] leading-[20px] text-[var(--settings-description-color)]">
            {t("MarketCanopyAIReady", "Connected to Market Canopy.")}
          </p>
          <ul className="text-[14px] leading-[20px] text-[var(--text-normal)]">
            {session.models.map((model) => (
              <li key={model.id}>{model.name}</li>
            ))}
          </ul>
        </>
      ) : (
        <p className="text-[14px] leading-[20px] text-[var(--settings-description-color)]">
          {STATE_MESSAGES[session.state]}
        </p>
      )}
    </div>
  );
};

export { MarketCanopyConnection };
