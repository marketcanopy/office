/*
 * Market Canopy Office: the desktop-sdk "additional" extension point, filled in.
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

// src/additional/create.cpp returns these. The manager side owns the account
// module for the browser process; the renderer side receives the AI session
// the browser pushes (SPEC 9).

#ifndef MARKETCANOPY_ADDITIONAL_H
#define MARKETCANOPY_ADDITIONAL_H

#include "../additional/manager.h"
#include "../additional/renderer.h"

class CMarketCanopyManagerAdditional : public CApplicationManagerAdditionalBase
{
public:
	explicit CMarketCanopyManagerAdditional(CAscApplicationManager* pManager);
	virtual ~CMarketCanopyManagerAdditional();

	virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefProcessId source_process,
										  CefRefPtr<CefProcessMessage> message, CCefView* view) override;
};

class CMarketCanopyRendererAdditional : public CApplicationRendererAdditionalBase
{
public:
	virtual bool OnProcessMessageReceived(CefRefPtr<client::ClientAppRenderer> app, CefRefPtr<CefBrowser> browser,
										  CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;
};

#endif // MARKETCANOPY_ADDITIONAL_H
