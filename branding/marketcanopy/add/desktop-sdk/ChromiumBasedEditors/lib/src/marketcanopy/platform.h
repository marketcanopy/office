/*
 * Market Canopy Office: what the account module needs from the OS and from CEF.
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

// The account module (account.h) is plain C++ written against these
// interfaces, so tests/account drives it with fakes and the contract
// fixtures. browser_glue.cpp implements them on CEF's UI thread; the credential
// store and the lock have one implementation per OS.

#ifndef MARKETCANOPY_PLATFORM_H
#define MARKETCANOPY_PLATFORM_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace NSMarketCanopy
{
	// SPEC 5: the OS credential store, one service, accounts "installation" and "grant|<apiOrigin>".
	class ICredentialStore
	{
	public:
		enum EStatus
		{
			Ok,
			NotFound,
			Unavailable, // no secure store, or the person declined access: memory-only session (5.4 rule 6)
			Failed
		};
		virtual ~ICredentialStore() {}
		// May block (a keychain prompt, a D-Bus round trip): called off the UI thread.
		virtual EStatus Read(const std::string& account, std::string& value) = 0;
		virtual EStatus Write(const std::string& account, const std::string& value) = 0;
		virtual EStatus Delete(const std::string& account) = 0;
	};

	// SPEC 5.4: the owner-only rotation lock file shared with the desktop app.
	class IStoreLock
	{
	public:
		virtual ~IStoreLock() {}
		// Blocks up to `timeoutMs` (called off the UI thread). false: not acquired.
		virtual bool Acquire(int timeoutMs) = 0;
		virtual void Release() = 0;
	};

	// Created per OS in credential_store_<os>.cpp and file_lock.cpp.
	ICredentialStore* CreateOsCredentialStore(const std::string& service);
	IStoreLock* CreateOsStoreLock(const std::string& service);
	// "windows", "macos" or "linux" (X-Client-Platform)
	const char* OsPlatformName();

	struct CHttpRequest
	{
		std::string method;
		std::string url;
		std::vector<std::pair<std::string, std::string> > headers;
		std::string body;
	};

	struct CHttpResponse
	{
		int status;
		std::string body;
		bool networkError; // no HTTP answer at all: DNS, TLS, timeout, offline
		CHttpResponse() : status(0), networkError(true) {}
	};

	// Everything the module asks of the outside world. All callbacks run on
	// the thread the module lives on (CEF's UI thread in the product).
	class IEnvironment
	{
	public:
		virtual ~IEnvironment() {}

		virtual long long NowMs() = 0;
		virtual void Post(int delayMs, const std::function<void()>& task) = 0;
		// Runs `work` on a thread that may block, then `done` back on the module's thread.
		virtual void RunBlocking(const std::function<void()>& work, const std::function<void()>& done) = 0;
		// Never sends cookies or stored credentials; `done` runs once.
		virtual void Send(const CHttpRequest& request, int timeoutMs, const std::function<void(const CHttpResponse&)>& done) = 0;

		// Start pages: on_native_message("mc:account", <state JSON>)
		virtual void PublishState(const std::string& stateJson) = 0;
		// Start pages: portal:login for the Market Canopy connection (SPEC 7.2)
		virtual void AnnounceProvider(const std::string& portalLoginJson) = 0;
		// Delete the app origin's cookies and tell start pages portal:logout (SPEC 7.5, ADR 0526 decision 5)
		virtual void ClearProviderSession(const std::string& appOrigin) = 0;
		// Renderers that asked: the cached getMarketCanopyAiSession() result and when it expires (0: never)
		virtual void PublishAiSession(const std::string& sessionJson, long long expiresAtMs) = 0;
		// The system browser (standalone sign-in, SPEC 6.4)
		virtual void OpenBrowser(const std::string& url) = 0;
		// Navigate the browser that loaded the connect page to it again as a form POST (SPEC 7.1)
		virtual void LoadConnectPost(int browserId, const std::string& url, const std::string& formBody) = 0;
		// A line for the log. Callers never pass a token, a code, a URL with a query or a path.
		virtual void Log(const std::string& line) = 0;
	};
}

#endif // MARKETCANOPY_PLATFORM_H
