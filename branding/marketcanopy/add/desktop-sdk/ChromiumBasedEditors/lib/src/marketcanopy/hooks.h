/*
 * Market Canopy Office: the calls the patched upstream sources make into the account module.
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

// Each upstream file the patch queue touches calls exactly one of these, so
// the patches stay a line or two per seam (ADR 0529 decision 2). The browser
// process hooks are in browser_glue.cpp, the renderer ones in renderer_glue.cpp.

#ifndef MARKETCANOPY_HOOKS_H
#define MARKETCANOPY_HOOKS_H

#include <string>

#include "include/cef_browser.h"
#include "include/cef_frame.h"

class CCefView;
class CAscApplicationManager;

namespace NSMarketCanopy
{
	// ---- browser process ----

	// cefview.cpp, "on_exec_command": true when the command was the account
	// module's (an "mc:" command, accepted from a start page only, or a
	// start page's "portal:logout" of Market Canopy, which it observes).
	bool OnExecCommand(CCefView* view, CefRefPtr<CefBrowser> browser, const std::wstring& command, const std::wstring& param);

	// cefview.cpp, OnLoadEnd for a main frame: launch on the first start page,
	// and the connect page's web-session POST (SPEC 7.1).
	void OnMainFrameLoaded(CCefView* view, CefRefPtr<CefBrowser> browser, int httpStatusCode);

	// CAscApplicationManager::CallCommand. The shells pass every launch
	// argument and opened URL as "mc:scheme:<argument>" first; 1 means the
	// account module consumed it (a sign-in callback, or a link SPEC 4.1
	// refuses) and the shell must not open it.
	int CallCommand(CAscApplicationManager* manager, const std::wstring& command);

	// ---- renderer process ----

	extern const char* const kAiSessionMethod; // "getMarketCanopyAiSession"

	// The frame may call getMarketCanopyAiSession() (SPEC 9).
	bool AiSessionCallerAllowed(CefRefPtr<CefFrame> frame, const std::wstring& systemPluginsDir);
	// The method's synchronous result for an allowed caller.
	std::string AiSession(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame);
	// client_renderer_wrapper.cpp OnContextCreated: an AI plugin frame asks for
	// the session ahead of its first call.
	void OnRendererContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const std::wstring& systemPluginsDir);
}

#endif // MARKETCANOPY_HOOKS_H
