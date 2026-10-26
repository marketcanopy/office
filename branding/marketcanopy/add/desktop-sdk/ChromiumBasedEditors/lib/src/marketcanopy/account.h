/*
 * Market Canopy Office: the account module (ADR 0524, ADR 0526 decision 2).
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

// Office's Market Canopy identity. It reads the linked grant the desktop app
// provisions (or writes one after a standalone browser sign-in), rotates it
// under the shared lock, keeps every access token in memory, fetches the
// bootstrap, mints the one-time web-session code for the cloud connect page
// and the office-ai token for the two AI plugins. Section numbers are
// contracts/SPEC.md's.
//
// Single-threaded: every method, and every callback it receives through
// IEnvironment, runs on one thread.

#ifndef MARKETCANOPY_ACCOUNT_H
#define MARKETCANOPY_ACCOUNT_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "contract.h"
#include "platform.h"

namespace NSMarketCanopy
{
	class CAccount
	{
	public:
		enum EState
		{
			Starting,
			SignedOut,
			SigningIn,      // the browser sign-in is open
			Connecting,
			SignedIn,
			UpdateRequired, // 426 or bootstrap office.updateRequired: cloud and AI off, local editing on
			Offline,
			NoOrganization, // NOT_A_MEMBER, or no active membership
			Failed          // the API answered something Office cannot use
		};

		struct CConfig
		{
			EFlavor flavor;
			std::string version; // release form, 10.0.0-mc.3
			CConfig() : flavor(EFlavor::Production) {}
		};

		CAccount(IEnvironment* environment, ICredentialStore* store, IStoreLock* lock, const CConfig& config);
		~CAccount();

		// Launch (5.4 rule 5): read the entries and connect when a grant is there.
		void Start();
		// The start window gained focus: re-read the grant entry (5.4 rule 5).
		void OnActivate();
		// The start page's "Sign in to Market Canopy" (6.4).
		void SignIn();
		void CancelSignIn();
		void SignOut();
		void SelectOrganization(const std::string& organizationId);

		// An mc-office: URL that reached the app. true: it was ours to consume
		// (a sign-in callback, or a link refused under 4.1), so nothing else may
		// open it. false: a valid document link the caller may load.
		bool HandleSchemeUrl(const std::string& url);

		// A main frame finished loading `url` in browser `browserId`. true when
		// it is the connect page and a web-session POST is on its way (7.1).
		bool OnPageLoaded(int browserId, const std::string& url);

		// A renderer asked for getMarketCanopyAiSession()'s value (9).
		void RequestAiSession();

		EState GetState() const { return m_state; }
		static const char* StateName(EState state);
		// The start page's view of the account: no token, code or device id.
		std::string StateJson() const;
		bool CredentialStoreAvailable() const { return m_storeAvailable; }

		// Tests: the cached AI session result the renderers were sent last.
		const std::string& LastAiSession() const { return m_lastAiSession; }

	private:
		struct CStoreRead
		{
			ICredentialStore::EStatus status;
			std::string raw;
			CStoreRead() : status(ICredentialStore::Failed) {}
		};

		typedef std::function<void(bool)> TDone;
		typedef std::function<void(const CApiResult&)> TApiDone;

		IEnvironment* m_env;
		ICredentialStore* m_store;
		IStoreLock* m_lock;
		CConfig m_config;
		COrigins m_origins;
		std::string m_clientVersion;

		EState m_state;
		bool m_storeAvailable;
		std::string m_installationId;
		bool m_haveGrant;
		CGrant m_grant;
		std::string m_accessToken;
		long long m_accessExpiresMs;
		bool m_refreshing;
		std::vector<TDone> m_refreshWaiters;
		bool m_haveBootstrap;
		CBootstrap m_bootstrap;
		long long m_bootstrapAtMs;
		std::string m_lastError;
		std::string m_storeUrl;
		unsigned long long m_epoch; // bumped on sign-out and account change; stale callbacks drop

		// standalone sign-in (6.4)
		std::string m_pendingVerifier;
		std::string m_pendingState;
		long long m_pendingSinceMs;

		// office-ai token (8.2), memory only
		std::string m_aiToken;
		long long m_aiExpiresMs;
		std::string m_aiOrganizationId;
		long long m_aiWantedAtMs;
		bool m_aiMinting;
		std::string m_lastAiSession;

		// connect page POSTs per browser: count and first time (7.1, retry silently once)
		std::map<int, std::pair<int, long long> > m_connectAttempts;

		bool m_signedOutPoll;

		void SetState(EState state, const std::string& error = std::string());
		void Publish();
		void PublishAi();
		void ScheduleSignedOutPoll();

		void ReadEntries(const std::function<void(const CStoreRead& installation, const CStoreRead& grant)>& done);
		void AdoptGrantRead(const CStoreRead& read, bool fromActivation);
		void Connect();
		void EnsureAccessToken(const TDone& done);
		void Refresh(const TDone& done);
		void FinishRefresh(bool ok);
		void FetchBootstrap(bool retried);
		void WriteGrantLocked(const CGrant& grant, const std::function<void(bool)>& done);
		void DeleteGrantLocked(const std::string& deviceSessionId, const std::function<void()>& done);
		void LoseSession(const std::string& reason, bool deleteGrant);
		void ResetSession();

		void Api(const std::string& method, const std::string& path, const std::string& body, bool withToken,
				 const TApiDone& done);
		void ApiWithToken(const std::string& method, const std::string& path, const std::string& body, const TApiDone& done,
						  bool retried = false);
		bool HandleCommonError(const CApiResult& result);

		void FinishSignIn(const std::string& code);
		void MintAiToken();
	};
}

#endif // MARKETCANOPY_ACCOUNT_H
