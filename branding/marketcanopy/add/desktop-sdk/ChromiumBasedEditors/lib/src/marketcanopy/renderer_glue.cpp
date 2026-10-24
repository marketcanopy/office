/*
 * Market Canopy Office: getMarketCanopyAiSession() in the renderer process (SPEC 9).
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

// The method is synchronous and CEF has no synchronous IPC, so the browser
// process pushes the session to the frames of the two AI plugins that asked
// (browser_glue.cpp checks each frame's URL itself) and this cache answers.
// An AI frame asks when its context is created, again when its token has less
// than a minute left, and on every call after the browser stopped answering;
// until an answer arrives the method says "offline". It never returns a token
// past its expiry.

#include "additional.h"

#include <chrono>
#include <map>

#include "contract.h"
#include "hooks.h"

#ifdef MESSAGE_IN_BROWSER
#define MC_SEND_TO_BROWSER(frame, message) (frame)->GetBrowser()->SendProcessMessage(PID_BROWSER, message)
#else
#define MC_SEND_TO_BROWSER(frame, message) (frame)->SendProcessMessage(PID_BROWSER, message)
#endif

namespace NSMarketCanopy
{
	const char* const kAiSessionMethod = "getMarketCanopyAiSession";

	namespace
	{
		const long long kRefreshAheadMs = 60000;
		const long long kExpiryMarginMs = 5000;
		const long long kAskEveryMs = 5000;

		struct CCachedSession
		{
			std::string json;
			long long expiresAtMs; // 0: a state with no token
			long long askedAtMs;
			CCachedSession() : expiresAtMs(0), askedAtMs(0) {}
		};

		// One renderer process can host several AI frames; each keeps its own copy.
		std::map<long long, CCachedSession> g_sessions;

		long long NowMs()
		{
			return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::system_clock::now().time_since_epoch())
				.count();
		}

		void Ask(CefRefPtr<CefFrame> frame, CCachedSession& cached)
		{
			long long now = NowMs();
			if (cached.askedAtMs && now - cached.askedAtMs < kAskEveryMs)
				return;
			cached.askedAtMs = now;
			CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("mc_ai_session_request");
			message->GetArgumentList()->SetString(0, CefString(std::to_string(frame->GetIdentifier())));
			MC_SEND_TO_BROWSER(frame, message);
		}
	}

	bool AiSessionCallerAllowed(CefRefPtr<CefFrame> frame, const std::wstring& systemPluginsDir)
	{
		return frame && IsAiSessionFrame(frame->GetURL().ToWString(), systemPluginsDir);
	}

	std::string AiSession(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame)
	{
		CCachedSession& cached = g_sessions[frame->GetIdentifier()];
		long long now = NowMs();
		if (cached.json.empty())
		{
			Ask(frame, cached);
			return AiSessionState("offline");
		}
		if (cached.expiresAtMs)
		{
			if (cached.expiresAtMs - now <= kExpiryMarginMs)
			{
				Ask(frame, cached);
				return AiSessionState("offline");
			}
			if (cached.expiresAtMs - now <= kRefreshAheadMs)
				Ask(frame, cached);
		}
		return cached.json;
	}

	void OnRendererContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const std::wstring& systemPluginsDir)
	{
		if (!AiSessionCallerAllowed(frame, systemPluginsDir))
			return;
		CCachedSession& cached = g_sessions[frame->GetIdentifier()];
		cached = CCachedSession(); // a new document in this frame starts over
		Ask(frame, cached);
	}
}

bool CMarketCanopyRendererAdditional::OnProcessMessageReceived(CefRefPtr<client::ClientAppRenderer>,
															   CefRefPtr<CefBrowser> browser, CefProcessId,
															   CefRefPtr<CefProcessMessage> message)
{
	if (message->GetName() != "mc_ai_session")
		return false;
	CefRefPtr<CefListValue> args = message->GetArgumentList();
	if (args->GetSize() < 3 || args->GetType(0) != VTYPE_STRING || args->GetType(1) != VTYPE_DOUBLE ||
		args->GetType(2) != VTYPE_STRING)
		return true;
	long long frameId = strtoll(args->GetString(2).ToString().c_str(), NULL, 10);
	if (!browser || !browser->GetFrame(frameId))
		return true;
	NSMarketCanopy::CCachedSession& cached = NSMarketCanopy::g_sessions[frameId];
	cached.json = args->GetString(0).ToString();
	cached.expiresAtMs = (long long)args->GetDouble(1);
	cached.askedAtMs = 0;
	return true;
}
