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

#include "account.h"

#include <algorithm>

#include "crypto.h"

namespace NSMarketCanopy
{
	namespace
	{
		const int kLockTimeoutMs = 30000;     // 5.4 rule 2
		const int kRotationTimeoutMs = 25000; // a rotation finishes inside the 30-second lock
		const int kApiTimeoutMs = 20000;
		const long long kTokenMarginMs = 60000;          // refresh when less than a minute is left (9)
		const long long kSignInWindowMs = 5 * 60 * 1000; // 4.2: a callback older than 5 minutes is ignored
		const int kSignedOutPollMs = 60000;              // 5.4 rule 5
		const long long kBootstrapStaleMs = 5 * 60 * 1000;
		const long long kAiKeepWarmMs = 30 * 60 * 1000;  // keep the AI token fresh this long after the last ask
		const long long kAiMaxLifetimeMs = 15 * 60 * 1000;

		std::string Str(const Json& value)
		{
			return value.IsString() ? value.AsString() : std::string();
		}

		bool StartsWith(const std::string& s, const std::string& prefix)
		{
			return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
		}
	}

	CAccount::CAccount(IEnvironment* environment, ICredentialStore* store, IStoreLock* lock, const CConfig& config)
		: m_env(environment), m_store(store), m_lock(lock), m_config(config), m_state(Starting), m_storeAvailable(store != NULL),
		  m_haveGrant(false), m_accessExpiresMs(0), m_refreshing(false), m_haveBootstrap(false), m_bootstrapAtMs(0), m_epoch(1),
		  m_pendingSinceMs(0), m_aiExpiresMs(0), m_aiWantedAtMs(0), m_aiMinting(false), m_signedOutPoll(false)
	{
		m_origins = Origins();
		if (!ClientVersionHeader(config.version, m_clientVersion))
			m_clientVersion = "0";
	}

	CAccount::~CAccount()
	{
	}

	const char* CAccount::StateName(EState state)
	{
		switch (state)
		{
		case Starting: return "starting";
		case SignedOut: return "signed_out";
		case SigningIn: return "signing_in";
		case Connecting: return "connecting";
		case SignedIn: return "signed_in";
		case UpdateRequired: return "update_required";
		case Offline: return "offline";
		case NoOrganization: return "no_organization";
		case Failed: return "failed";
		}
		return "failed";
	}

	// ---- state -----------------------------------------------------------

	void CAccount::SetState(EState state, const std::string& error)
	{
		m_state = state;
		m_lastError = error;
		Publish();
		PublishAi();
		if (state == SignedOut || state == Offline || state == Failed)
			ScheduleSignedOutPoll();
	}

	std::string CAccount::StateJson() const
	{
		Json value = Json::MakeObject();
		value.Set("state", Json::MakeString(StateName(m_state)));
		value.Set("credentialStore", Json::MakeString(m_storeAvailable ? "available" : "unavailable"));
		value.Set("error", m_lastError.empty() ? Json() : Json::MakeString(m_lastError));
		value.Set("appOrigin", Json::MakeString(m_origins.app));
		if (m_haveBootstrap && m_haveGrant)
		{
			Json user = Json::MakeObject();
			user.Set("displayName", Json::MakeString(m_bootstrap.displayName));
			user.Set("email", Json::MakeString(m_bootstrap.email));
			value.Set("user", user);
			Json organizations = Json::MakeArray();
			for (size_t i = 0; i < m_bootstrap.organizations.size(); ++i)
			{
				Json organization = Json::MakeObject();
				organization.Set("id", Json::MakeString(m_bootstrap.organizations[i].id));
				organization.Set("name", Json::MakeString(m_bootstrap.organizations[i].name));
				organizations.Push(organization);
			}
			value.Set("organizations", organizations);
			value.Set("activeOrganizationId", m_bootstrap.activeOrganizationId.empty() ? Json() : Json::MakeString(m_bootstrap.activeOrganizationId));
			Json docs = Json::MakeObject();
			docs.Set("enabled", Json::MakeBool(m_bootstrap.docsEnabled));
			value.Set("docs", docs);
			Json ai = Json::MakeObject();
			ai.Set("enabled", Json::MakeBool(m_bootstrap.aiEnabled));
			ai.Set("disabledReason", m_bootstrap.aiDisabledReason.empty() ? Json() : Json::MakeString(m_bootstrap.aiDisabledReason));
			value.Set("ai", ai);
		}
		Json update = Json::MakeObject();
		update.Set("required", Json::MakeBool(m_state == UpdateRequired));
		update.Set("storeUrl", Json::MakeString(m_storeUrl));
		update.Set("latestVersion", Json::MakeString(m_haveBootstrap ? m_bootstrap.latestVersion : std::string()));
		value.Set("update", update);
		return value.Dump();
	}

	void CAccount::Publish()
	{
		m_env->PublishState(StateJson());
	}

	void CAccount::PublishAi()
	{
		std::string json;
		long long expires = 0;
		switch (m_state)
		{
		case Starting:
		case Connecting:
			if (!m_haveGrant && m_state == Starting)
				return; // not known yet; a renderer that asks gets "offline" until the first answer
			json = AiSessionState("offline");
			break;
		case SignedOut:
		case SigningIn:
			json = AiSessionState("signed_out");
			break;
		case Offline:
		case Failed:
			json = AiSessionState("offline");
			break;
		case UpdateRequired:
			json = AiSessionState("update_required");
			break;
		case NoOrganization:
			json = AiSessionState("not_entitled");
			break;
		case SignedIn:
			if (!m_haveBootstrap || !m_bootstrap.aiEnabled)
			{
				json = AiSessionState("not_entitled");
				break;
			}
			if (!m_aiToken.empty() && m_aiExpiresMs - m_env->NowMs() > kTokenMarginMs)
			{
				json = AiSessionReady(m_bootstrap.aiBaseUrl, m_aiToken, FormatInstant(m_aiExpiresMs), m_aiOrganizationId,
									  m_bootstrap.models, m_bootstrap.defaultModel);
				expires = m_aiExpiresMs;
				break;
			}
			if (m_aiWantedAtMs > 0)
				MintAiToken();
			return; // published once the token is there
		}
		if (json == m_lastAiSession && expires == 0)
			return;
		m_lastAiSession = json;
		m_env->PublishAiSession(json, expires);
	}

	void CAccount::ScheduleSignedOutPoll()
	{
		if (m_signedOutPoll)
			return;
		m_signedOutPoll = true;
		m_env->Post(kSignedOutPollMs, [this]() {
			m_signedOutPoll = false;
			if (m_state == SignedOut)
				OnActivate();
			else if ((m_state == Offline || m_state == Failed) && m_haveGrant)
				Connect();
		});
	}

	// ---- credential store ------------------------------------------------

	void CAccount::ReadEntries(const std::function<void(const CStoreRead&, const CStoreRead&)>& done)
	{
		ICredentialStore* store = m_store;
		IStoreLock* lock = m_lock;
		std::string grantAccount = GrantAccount(m_origins.api);
		std::shared_ptr<CStoreRead> installation(new CStoreRead());
		std::shared_ptr<CStoreRead> grant(new CStoreRead());
		m_env->RunBlocking(
			[store, lock, grantAccount, installation, grant]() {
				std::string id;
				installation->status = store->Read(kInstallationAccount, installation->raw);
				bool usable = installation->status == ICredentialStore::Ok && ReadInstallation(installation->raw, id);
				if (!usable && installation->status != ICredentialStore::Unavailable)
				{
					// 5.1: whoever needs it first creates it, under the lock, after a re-read
					if (lock->Acquire(kLockTimeoutMs))
					{
						installation->status = store->Read(kInstallationAccount, installation->raw);
						if (!(installation->status == ICredentialStore::Ok && ReadInstallation(installation->raw, id)) &&
							installation->status != ICredentialStore::Unavailable)
						{
							std::string created = WriteInstallation(NewUuidV4());
							installation->status = store->Write(kInstallationAccount, created);
							if (installation->status == ICredentialStore::Ok)
								installation->raw = created;
						}
						lock->Release();
					}
					else
						installation->status = ICredentialStore::Failed;
				}
				grant->status = store->Read(grantAccount, grant->raw);
			},
			[done, installation, grant]() { done(*installation, *grant); });
	}

	void CAccount::Start()
	{
		SetState(Starting);
		if (!m_store)
		{
			m_installationId = NewUuidV4();
			SetState(SignedOut);
			return;
		}
		ReadEntries([this](const CStoreRead& installation, const CStoreRead& grant) {
			if (installation.status == ICredentialStore::Unavailable || grant.status == ICredentialStore::Unavailable)
			{
				m_storeAvailable = false;
				m_installationId = NewUuidV4();
				m_env->Log("credential store unavailable: memory-only session");
				SetState(SignedOut);
				return;
			}
			if (!(installation.status == ICredentialStore::Ok && ReadInstallation(installation.raw, m_installationId)))
			{
				m_installationId = NewUuidV4();
				m_env->Log("installation entry could not be created; using a session-only installation id");
			}
			AdoptGrantRead(grant, false);
		});
	}

	void CAccount::OnActivate()
	{
		if (m_state == Starting || m_state == Connecting || !m_storeAvailable)
			return;
		ICredentialStore* store = m_store;
		std::string account = GrantAccount(m_origins.api);
		std::shared_ptr<CStoreRead> read(new CStoreRead());
		unsigned long long epoch = m_epoch;
		m_env->RunBlocking([store, account, read]() { read->status = store->Read(account, read->raw); },
						   [this, read, epoch]() {
							   if (epoch != m_epoch || m_state == Connecting)
								   return;
							   AdoptGrantRead(*read, true);
						   });
	}

	void CAccount::AdoptGrantRead(const CStoreRead& read, bool fromActivation)
	{
		if (read.status == ICredentialStore::Unavailable)
		{
			m_storeAvailable = false;
			if (!m_haveGrant)
				SetState(SignedOut);
			return;
		}
		CGrant grant;
		EGrantRead result = EGrantRead::Missing;
		if (read.status == ICredentialStore::Ok)
			result = ReadGrant(&read.raw, m_origins.api, grant);
		else if (read.status == ICredentialStore::Failed)
			result = EGrantRead::Unreadable;

		if (result != EGrantRead::Present)
		{
			if (m_haveGrant && fromActivation)
			{
				// signed out by the desktop app (it deletes after revoking, 5.4 rule 4)
				LoseSession("SIGNED_OUT_ELSEWHERE", false);
				return;
			}
			if (!m_haveGrant)
			{
				if (result != EGrantRead::Missing)
					m_env->Log(std::string("grant entry absent: ") + GrantReadName(result));
				if (m_state != SignedOut && m_state != SigningIn)
					SetState(SignedOut);
				else
					ScheduleSignedOutPoll();
			}
			return;
		}

		if (!m_haveGrant)
		{
			m_grant = grant;
			m_haveGrant = true;
			m_pendingState.clear();
			m_pendingVerifier.clear();
			Connect();
			return;
		}
		if (grant.userId != m_grant.userId || grant.deviceSessionId != m_grant.deviceSessionId)
		{
			// ACCOUNT_CHANGED (13): another person, or a new linked grant
			m_env->Log("grant entry changed: reconnecting");
			ResetSession();
			m_grant = grant;
			m_haveGrant = true;
			Connect();
			return;
		}
		if (grant.refreshToken != m_grant.refreshToken)
			m_grant.refreshToken = grant.refreshToken;
		if (m_state == SignedIn && m_env->NowMs() - m_bootstrapAtMs > kBootstrapStaleMs)
			FetchBootstrap(false);
		else if (m_state == Offline || m_state == Failed || m_state == NoOrganization)
			Connect();
	}

	void CAccount::WriteGrantLocked(const CGrant& grant, const std::function<void(bool)>& done)
	{
		if (!m_storeAvailable)
		{
			done(true);
			return;
		}
		ICredentialStore* store = m_store;
		IStoreLock* lock = m_lock;
		std::string account = GrantAccount(m_origins.api);
		std::string value = WriteGrant(grant);
		std::shared_ptr<bool> ok(new bool(false));
		m_env->RunBlocking(
			[store, lock, account, value, ok]() {
				if (!lock->Acquire(kLockTimeoutMs))
					return;
				*ok = store->Write(account, value) == ICredentialStore::Ok;
				lock->Release();
			},
			[done, ok]() { done(*ok); });
	}

	void CAccount::DeleteGrantLocked(const std::string& deviceSessionId, const std::function<void()>& done)
	{
		if (!m_storeAvailable || deviceSessionId.empty())
		{
			done();
			return;
		}
		ICredentialStore* store = m_store;
		IStoreLock* lock = m_lock;
		std::string account = GrantAccount(m_origins.api);
		std::string apiOrigin = m_origins.api;
		m_env->RunBlocking(
			[store, lock, account, apiOrigin, deviceSessionId]() {
				if (!lock->Acquire(kLockTimeoutMs))
					return;
				// 5.4 rule 4: compare-and-delete, so a newer grant written meanwhile stays
				std::string raw;
				CGrant current;
				ICredentialStore::EStatus status = store->Read(account, raw);
				EGrantRead read = status == ICredentialStore::Ok ? ReadGrant(&raw, apiOrigin, current) : EGrantRead::Missing;
				if (MayDelete(read, current, deviceSessionId))
					store->Delete(account);
				lock->Release();
			},
			done);
	}

	// ---- session ---------------------------------------------------------

	void CAccount::ResetSession()
	{
		bool hadSession = m_haveGrant || m_haveBootstrap;
		++m_epoch;
		m_accessToken.clear();
		m_accessExpiresMs = 0;
		m_haveBootstrap = false;
		m_bootstrap = CBootstrap();
		m_aiToken.clear();
		m_aiExpiresMs = 0;
		m_aiOrganizationId.clear();
		m_aiMinting = false;
		m_connectAttempts.clear();
		m_storeUrl.clear();
		std::vector<TDone> waiters;
		waiters.swap(m_refreshWaiters);
		m_refreshing = false;
		if (hadSession)
			m_env->ClearProviderSession(m_origins.app);
		for (size_t i = 0; i < waiters.size(); ++i)
			waiters[i](false);
	}

	void CAccount::LoseSession(const std::string& reason, bool deleteGrant)
	{
		std::string revoked = m_grant.deviceSessionId;
		bool hadGrant = m_haveGrant;
		ResetSession();
		m_haveGrant = false;
		m_grant = CGrant();
		SetState(SignedOut, reason);
		if (deleteGrant && hadGrant)
			DeleteGrantLocked(revoked, []() {});
	}

	void CAccount::Connect()
	{
		if (!m_haveGrant)
		{
			SetState(SignedOut);
			return;
		}
		SetState(Connecting);
		unsigned long long epoch = m_epoch;
		EnsureAccessToken([this, epoch](bool ok) {
			if (epoch == m_epoch && ok)
				FetchBootstrap(false);
		});
	}

	void CAccount::EnsureAccessToken(const TDone& done)
	{
		if (!m_accessToken.empty() && m_accessExpiresMs - m_env->NowMs() > kTokenMarginMs)
		{
			done(true);
			return;
		}
		Refresh(done);
	}

	void CAccount::FinishRefresh(bool ok)
	{
		m_refreshing = false;
		std::vector<TDone> waiters;
		waiters.swap(m_refreshWaiters);
		for (size_t i = 0; i < waiters.size(); ++i)
			waiters[i](ok);
	}

	void CAccount::Refresh(const TDone& done)
	{
		m_refreshWaiters.push_back(done);
		if (m_refreshing)
			return;
		if (!m_haveGrant)
		{
			FinishRefresh(false);
			return;
		}
		m_refreshing = true;
		unsigned long long epoch = m_epoch;

		// Memory-only session: no lock, no entry.
		std::shared_ptr<bool> locked(new bool(false));
		std::shared_ptr<CStoreRead> read(new CStoreRead());
		ICredentialStore* store = m_storeAvailable ? m_store : NULL;
		IStoreLock* lock = m_lock;
		std::string account = GrantAccount(m_origins.api);

		std::function<void()> afterRead = [this, epoch, locked, read]() {
			if (epoch != m_epoch)
			{
				if (*locked)
					m_lock->Release();
				return;
			}
			std::function<void()> release = [this, locked]() {
				if (*locked)
				{
					*locked = false;
					m_lock->Release();
				}
			};
			if (m_storeAvailable)
			{
				if (!*locked)
				{
					m_env->Log("rotation lock busy for 30 seconds");
					SetState(Failed, "LOCK_TIMEOUT");
					FinishRefresh(false);
					return;
				}
				// 5.4 rule 1: re-read after acquiring
				CGrant current;
				EGrantRead result = read->status == ICredentialStore::Ok ? ReadGrant(&read->raw, m_origins.api, current) : EGrantRead::Missing;
				if (result != EGrantRead::Present)
				{
					release();
					FinishRefresh(false);
					LoseSession("SIGNED_OUT_ELSEWHERE", false);
					return;
				}
				if (current.userId != m_grant.userId || current.deviceSessionId != m_grant.deviceSessionId)
				{
					release();
					FinishRefresh(false);
					CStoreRead copy = *read;
					AdoptGrantRead(copy, true);
					return;
				}
				m_grant.refreshToken = current.refreshToken;
			}

			Json body = Json::MakeObject();
			body.Set("refreshToken", Json::MakeString(m_grant.refreshToken));
			CHttpRequest request;
			request.method = "POST";
			request.url = m_origins.api + "/v1/auth/refresh";
			request.headers.push_back(std::make_pair("Content-Type", "application/json"));
			request.headers.push_back(std::make_pair("X-Client-App", kClientApp));
			request.headers.push_back(std::make_pair("X-Client-Platform", OsPlatformName()));
			request.headers.push_back(std::make_pair("X-Client-Version", m_clientVersion));
			request.headers.push_back(std::make_pair("X-Device-Id", m_grant.installationId.empty() ? m_installationId : m_grant.installationId));
			request.body = body.Dump();
			m_env->Send(request, kRotationTimeoutMs, [this, epoch, release](const CHttpResponse& response) {
				CApiResult result = ReadEnvelope(response.status, response.body, response.networkError);
				if (epoch != m_epoch)
				{
					release();
					return;
				}
				if (result.kind == CApiResult::Ok)
				{
					std::string access = Str(result.data.Get("accessToken"));
					std::string rotated = Str(result.data.Get("refreshToken"));
					double expiresIn = result.data.Get("expiresIn").IsNumber() ? result.data.Get("expiresIn").AsNumber() : 0;
					if (access.empty() || !StartsWith(rotated, "dsr_") || expiresIn <= 0)
					{
						release();
						SetState(Failed, "UNEXPECTED_RESPONSE");
						FinishRefresh(false);
						return;
					}
					m_accessToken = access;
					m_accessExpiresMs = m_env->NowMs() + (long long)(std::min(expiresIn, 3600.0) * 1000);
					CGrant next = m_grant;
					next.refreshToken = rotated;
					next.writtenBy = "office";
					m_grant = next;
					if (!m_storeAvailable)
					{
						FinishRefresh(true);
						return;
					}
					// still under the lock: write the rotated value, then release (5.4 rule 2)
					ICredentialStore* store = m_store;
					std::string account = GrantAccount(m_origins.api);
					std::string value = WriteGrant(next);
					std::shared_ptr<bool> written(new bool(false));
					m_env->RunBlocking([store, account, value, written]() { *written = store->Write(account, value) == ICredentialStore::Ok; },
									   [this, release, written]() {
										   release();
										   if (!*written)
											   m_env->Log("could not store the rotated grant; the session lasts until Office closes");
										   FinishRefresh(true);
									   });
					return;
				}
				if (result.code == "UNAUTHORIZED" || result.code == "GRANT_REVOKED")
				{
					// 6.3: a 401 from a credential route is signed out; compare-and-delete under the held lock
					std::string revoked = m_grant.deviceSessionId;
					std::string apiOrigin = m_origins.api;
					ICredentialStore* store = m_storeAvailable ? m_store : NULL;
					std::string account = GrantAccount(m_origins.api);
					std::string code = result.code;
					m_env->RunBlocking(
						[store, account, apiOrigin, revoked]() {
							if (!store)
								return;
							std::string raw;
							CGrant current;
							EGrantRead read = store->Read(account, raw) == ICredentialStore::Ok ? ReadGrant(&raw, apiOrigin, current) : EGrantRead::Missing;
							if (MayDelete(read, current, revoked))
								store->Delete(account);
						},
						[this, release, code]() {
							release();
							FinishRefresh(false);
							LoseSession(code, false);
						});
					return;
				}
				release();
				if (result.code == "CLIENT_UPGRADE_REQUIRED")
				{
					m_storeUrl = Str(result.details.Get("storeUrl"));
					SetState(UpdateRequired, result.code);
				}
				else if (result.kind == CApiResult::Network || result.code == "NETWORK_UNAVAILABLE")
					SetState(Offline, "NETWORK_UNAVAILABLE");
				else
					SetState(Failed, result.code);
				FinishRefresh(false);
			});
		};

		if (!store)
		{
			afterRead();
			return;
		}
		m_env->RunBlocking(
			[store, lock, account, locked, read]() {
				*locked = lock->Acquire(kLockTimeoutMs);
				if (*locked)
					read->status = store->Read(account, read->raw);
			},
			afterRead);
	}

	// ---- API -------------------------------------------------------------

	void CAccount::Api(const std::string& method, const std::string& path, const std::string& body, bool withToken,
					   const TApiDone& done)
	{
		CHttpRequest request;
		request.method = method;
		request.url = m_origins.api + path;
		if (method != "GET")
			request.headers.push_back(std::make_pair("Content-Type", "application/json"));
		request.headers.push_back(std::make_pair("X-Client-App", kClientApp));
		request.headers.push_back(std::make_pair("X-Client-Platform", OsPlatformName()));
		request.headers.push_back(std::make_pair("X-Client-Version", m_clientVersion));
		request.headers.push_back(std::make_pair("X-Device-Id", m_haveGrant && !m_grant.installationId.empty() ? m_grant.installationId : m_installationId));
		if (withToken)
			request.headers.push_back(std::make_pair("Authorization", "Bearer " + m_accessToken));
		std::string organization = m_haveBootstrap ? m_bootstrap.activeOrganizationId : m_grant.lastActiveOrganizationId;
		if (withToken && !organization.empty())
			request.headers.push_back(std::make_pair("x-active-organization-id", organization));
		request.body = method == "GET" ? std::string() : body;
		m_env->Send(request, kApiTimeoutMs, [done](const CHttpResponse& response) {
			done(ReadEnvelope(response.status, response.body, response.networkError));
		});
	}

	void CAccount::ApiWithToken(const std::string& method, const std::string& path, const std::string& body, const TApiDone& done,
								bool retried)
	{
		unsigned long long epoch = m_epoch;
		EnsureAccessToken([this, epoch, method, path, body, done, retried](bool ok) {
			if (epoch != m_epoch)
				return;
			if (!ok)
				return; // Refresh already moved the state (signed out, offline, update required)
			Api(method, path, body, true, [this, epoch, method, path, body, done, retried](const CApiResult& result) {
				if (epoch != m_epoch)
					return;
				if (result.code == "UNAUTHORIZED" && !retried)
				{
					// the access token was refused before its time: rotate once and retry
					m_accessToken.clear();
					m_accessExpiresMs = 0;
					ApiWithToken(method, path, body, done, true);
					return;
				}
				done(result);
			});
		});
	}

	bool CAccount::HandleCommonError(const CApiResult& result)
	{
		if (result.kind == CApiResult::Network || result.code == "NETWORK_UNAVAILABLE")
		{
			SetState(Offline, "NETWORK_UNAVAILABLE");
			return true;
		}
		if (result.code == "CLIENT_UPGRADE_REQUIRED")
		{
			m_storeUrl = Str(result.details.Get("storeUrl"));
			SetState(UpdateRequired, result.code);
			return true;
		}
		if (result.code == "UNAUTHORIZED" || result.code == "GRANT_REVOKED" || result.code == "WRONG_AUDIENCE")
		{
			LoseSession(result.code, true);
			return true;
		}
		if (result.code == "NOT_A_MEMBER")
		{
			SetState(NoOrganization, result.code);
			return true;
		}
		return false;
	}

	void CAccount::FetchBootstrap(bool retried)
	{
		unsigned long long epoch = m_epoch;
		ApiWithToken("GET", "/v1/office/bootstrap", "", [this, epoch, retried](const CApiResult& result) {
			if (epoch != m_epoch)
				return;
			if (result.kind == CApiResult::Ok)
			{
				CBootstrap bootstrap;
				if (!ReadBootstrap(result.data, bootstrap) || bootstrap.userId != m_grant.userId)
				{
					SetState(Failed, "UNEXPECTED_RESPONSE");
					return;
				}
				m_bootstrap = bootstrap;
				m_haveBootstrap = true;
				m_bootstrapAtMs = m_env->NowMs();
				if (bootstrap.activeOrganizationId.empty())
				{
					SetState(NoOrganization, "NOT_A_MEMBER");
					return;
				}
				if (bootstrap.activeOrganizationId != m_grant.lastActiveOrganizationId)
				{
					m_grant.lastActiveOrganizationId = bootstrap.activeOrganizationId;
					// Remember it for the next launch, keeping whatever refresh token is stored now.
					if (m_storeAvailable)
					{
						ICredentialStore* store = m_store;
						IStoreLock* lock = m_lock;
						std::string account = GrantAccount(m_origins.api);
						std::string apiOrigin = m_origins.api;
						std::string deviceSessionId = m_grant.deviceSessionId;
						std::string organization = bootstrap.activeOrganizationId;
						m_env->RunBlocking(
							[store, lock, account, apiOrigin, deviceSessionId, organization]() {
								if (!lock->Acquire(kLockTimeoutMs))
									return;
								std::string raw;
								CGrant current;
								if (store->Read(account, raw) == ICredentialStore::Ok &&
									ReadGrant(&raw, apiOrigin, current) == EGrantRead::Present && current.deviceSessionId == deviceSessionId)
								{
									current.lastActiveOrganizationId = organization;
									store->Write(account, WriteGrant(current));
								}
								lock->Release();
							},
							[]() {});
					}
				}
				if (bootstrap.updateRequired)
				{
					SetState(UpdateRequired, "CLIENT_UPGRADE_REQUIRED");
					return;
				}
				SetState(SignedIn);
				if (bootstrap.docsEnabled)
				{
					// ADR 0526 decision 2: the start page lists Market Canopy as connected
					Json login = Json::MakeObject();
					login.Set("provider", Json::MakeString(kProviderId));
					login.Set("domain", Json::MakeString(bootstrap.docsAppOrigin));
					login.Set("uid", Json::MakeString(bootstrap.userId));
					login.Set("displayName", Json::MakeString(bootstrap.displayName.empty() ? bootstrap.email : bootstrap.displayName));
					login.Set("email", Json::MakeString(bootstrap.email));
					m_env->AnnounceProvider(login.Dump());
				}
				return;
			}
			if (result.code == "NOT_A_MEMBER" && !retried && !m_grant.lastActiveOrganizationId.empty())
			{
				// the remembered organization is gone: ask again with none selected
				m_grant.lastActiveOrganizationId.clear();
				FetchBootstrap(true);
				return;
			}
			if (!HandleCommonError(result))
				SetState(Failed, result.code);
		});
	}

	// ---- public actions ----------------------------------------------------

	void CAccount::SignIn()
	{
		if (m_state == SignedIn || m_state == Connecting || m_state == Starting)
			return;
		CPkce pkce;
		std::string stateBytes;
		if (!NewPkce(pkce) || !RandomBytes(32, stateBytes))
		{
			SetState(Failed, "NO_RANDOMNESS");
			return;
		}
		m_pendingVerifier = pkce.verifier;
		m_pendingState = Base64Url(stateBytes);
		m_pendingSinceMs = m_env->NowMs();
		std::string url = m_origins.app + "/auth/native?clientApp=" + kClientApp + "&codeChallenge=" + pkce.challenge +
						  "&state=" + m_pendingState;
		m_env->OpenBrowser(url);
		SetState(SigningIn);
		std::string pending = m_pendingState;
		m_env->Post((int)kSignInWindowMs + 1000, [this, pending]() {
			if (m_state == SigningIn && m_pendingState == pending)
			{
				m_pendingState.clear();
				m_pendingVerifier.clear();
				SetState(SignedOut, "SIGN_IN_TIMED_OUT");
			}
		});
	}

	void CAccount::CancelSignIn()
	{
		if (m_state != SigningIn)
			return;
		m_pendingState.clear();
		m_pendingVerifier.clear();
		SetState(SignedOut);
	}

	bool CAccount::HandleSchemeUrl(const std::string& url)
	{
		if (!IsSchemeUrl(url))
			return false;
		CSchemeLink link = ParseSchemeUrl(url);
		switch (link.kind)
		{
		case CSchemeLink::Open:
			return false;
		case CSchemeLink::Invalid:
			m_env->Log("refused an mc-office link: INVALID_LINK");
			return true;
		case CSchemeLink::CredentialInLink:
			m_env->Log("refused an mc-office link: CREDENTIAL_IN_LINK");
			return true;
		case CSchemeLink::AuthCallback:
		case CSchemeLink::AuthError:
			break;
		}
		// 4.2: only the sign-in this Office started in the last 5 minutes
		if (m_state != SigningIn || m_pendingState.empty() || link.state != m_pendingState ||
			m_env->NowMs() - m_pendingSinceMs > kSignInWindowMs)
		{
			m_env->Log("ignored a sign-in callback that matches no sign-in in progress");
			return true;
		}
		m_pendingState.clear();
		if (link.kind == CSchemeLink::AuthError)
		{
			m_pendingVerifier.clear();
			SetState(SignedOut, link.error);
			return true;
		}
		FinishSignIn(link.code);
		return true;
	}

	void CAccount::FinishSignIn(const std::string& code)
	{
		std::string verifier = m_pendingVerifier;
		m_pendingVerifier.clear();
		SetState(Connecting);
		Json body = Json::MakeObject();
		body.Set("code", Json::MakeString(code));
		body.Set("codeVerifier", Json::MakeString(verifier));
		unsigned long long epoch = m_epoch;
		Api("POST", "/v1/auth/native/exchange", body.Dump(), false, [this, epoch](const CApiResult& result) {
			if (epoch != m_epoch)
				return;
			if (result.kind != CApiResult::Ok)
			{
				if (result.code == "CLIENT_UPGRADE_REQUIRED")
				{
					m_storeUrl = Str(result.details.Get("storeUrl"));
					SetState(UpdateRequired, result.code);
				}
				else
					SetState(SignedOut, result.code.empty() ? std::string("UNEXPECTED_RESPONSE") : result.code);
				return;
			}
			const Json& data = result.data;
			CGrant grant;
			grant.apiOrigin = m_origins.api;
			grant.userId = Str(data.Get("user").Get("id"));
			grant.deviceSessionId = Str(data.Get("deviceSessionId"));
			grant.installationId = m_installationId;
			grant.refreshToken = Str(data.Get("refreshToken"));
			grant.issuedAt = FormatInstant(m_env->NowMs());
			grant.writtenBy = "office";
			std::string access = Str(data.Get("accessToken"));
			double expiresIn = data.Get("expiresIn").IsNumber() ? data.Get("expiresIn").AsNumber() : 0;
			if (!IsObjectId(grant.userId) || !IsObjectId(grant.deviceSessionId) || !StartsWith(grant.refreshToken, "dsr_") ||
				access.empty() || expiresIn <= 0)
			{
				// 1.1.0: the exchange never answers a second-factor challenge, so anything else is refused
				SetState(SignedOut, "UNEXPECTED_RESPONSE");
				return;
			}
			if (m_haveGrant)
				ResetSession();
			m_grant = grant;
			m_haveGrant = true;
			m_accessToken = access;
			m_accessExpiresMs = m_env->NowMs() + (long long)(std::min(expiresIn, 3600.0) * 1000);
			unsigned long long current = m_epoch;
			// 6.4 step 4. An account switch replaces the entry (5.1).
			WriteGrantLocked(grant, [this, current](bool ok) {
				if (current != m_epoch)
					return;
				if (!ok)
					m_env->Log("could not store the grant; the session lasts until Office closes");
				FetchBootstrap(false);
			});
		});
	}

	void CAccount::SignOut()
	{
		if (m_state == SigningIn)
		{
			CancelSignIn();
			return;
		}
		if (!m_haveGrant)
			return;
		unsigned long long epoch = m_epoch;
		// Revoke on the server first (5.1), then delete by compare-and-delete. Offline, the
		// local entry still goes: the person asked to be signed out of this computer.
		EnsureAccessToken([this, epoch](bool ok) {
			if (epoch != m_epoch)
				return;
			if (!ok)
			{
				LoseSession("SIGNED_OUT", true);
				return;
			}
			Api("POST", "/v1/auth/logout", "{}", true, [this, epoch](const CApiResult&) {
				if (epoch == m_epoch)
					LoseSession("SIGNED_OUT", true);
			});
		});
	}

	void CAccount::SelectOrganization(const std::string& organizationId)
	{
		if (m_state != SignedIn && m_state != NoOrganization)
			return;
		bool known = false;
		for (size_t i = 0; i < m_bootstrap.organizations.size(); ++i)
			if (m_bootstrap.organizations[i].id == organizationId)
				known = true;
		if (!known || organizationId == m_bootstrap.activeOrganizationId)
			return;
		// The web session and the AI token are bound to the organization (7.1, 8.2).
		m_env->ClearProviderSession(m_origins.app);
		m_aiToken.clear();
		m_aiExpiresMs = 0;
		m_connectAttempts.clear();
		// The request names it; the answer stores it as lastActiveOrganizationId.
		m_bootstrap.activeOrganizationId = organizationId;
		SetState(Connecting);
		FetchBootstrap(true);
	}

	bool CAccount::OnPageLoaded(int browserId, const std::string& url)
	{
		if (m_state != SignedIn || !m_haveBootstrap || !m_bootstrap.docsEnabled)
			return false;
		std::string connect = m_bootstrap.docsAppOrigin + m_bootstrap.connectPath;
		if (url != connect && url != connect + "?desktop=true")
			return false;
		long long now = m_env->NowMs();
		std::pair<int, long long>& attempts = m_connectAttempts[browserId];
		if (now - attempts.second > 2 * 60 * 1000)
			attempts = std::make_pair(0, now);
		if (attempts.first >= 2)
			return false; // 13: retry silently once, then the page says it couldn't connect
		++attempts.first;
		unsigned long long epoch = m_epoch;
		ApiWithToken("POST", "/v1/office/web-session", "{}", [this, epoch, browserId, connect](const CApiResult& result) {
			if (epoch != m_epoch)
				return;
			if (result.kind == CApiResult::Ok)
			{
				std::string code = Str(result.data.Get("code"));
				if (Str(result.data.Get("audience")) != "office-web" || !IsBase64Url(code, 43, 43))
				{
					m_env->Log("web-session answer unusable");
					return;
				}
				// 7.1: the code goes in a POST body Office sends itself, never in a URL
				m_env->LoadConnectPost(browserId, connect, "code=" + code);
				return;
			}
			if (!HandleCommonError(result))
				m_env->Log("web-session code refused: " + result.code);
		});
		return true;
	}

	void CAccount::RequestAiSession()
	{
		m_aiWantedAtMs = m_env->NowMs();
		// Always answer the renderer that asked, even when nothing changed.
		std::string previous = m_lastAiSession;
		m_lastAiSession.clear();
		PublishAi();
		if (m_lastAiSession.empty())
			m_lastAiSession = previous;
	}

	void CAccount::MintAiToken()
	{
		if (m_aiMinting)
			return;
		m_aiMinting = true;
		unsigned long long epoch = m_epoch;
		ApiWithToken("POST", "/v1/office/ai-session", "{}", [this, epoch](const CApiResult& result) {
			if (epoch != m_epoch)
				return;
			m_aiMinting = false;
			if (result.kind == CApiResult::Ok)
			{
				std::string token = Str(result.data.Get("accessToken"));
				long long expires = ParseInstant(Str(result.data.Get("expiresAt")));
				std::string organization = Str(result.data.Get("organizationId"));
				long long now = m_env->NowMs();
				if (token.empty() || expires <= now || organization != m_bootstrap.activeOrganizationId)
				{
					m_env->Log("AI session answer unusable");
					m_lastAiSession = AiSessionState("offline");
					m_env->PublishAiSession(m_lastAiSession, 0);
					return;
				}
				m_aiToken = token;
				m_aiExpiresMs = std::min(expires, now + kAiMaxLifetimeMs);
				m_aiOrganizationId = organization;
				PublishAi();
				// keep it fresh while the plugins are in use (9: never hand out an expired one)
				long long delay = m_aiExpiresMs - now - 90000;
				m_env->Post((int)std::max(delay, 1000LL), [this, epoch]() {
					if (epoch == m_epoch && m_state == SignedIn && m_env->NowMs() - m_aiWantedAtMs < kAiKeepWarmMs)
						MintAiToken();
				});
				return;
			}
			if (result.code == "AI_NOT_ENTITLED" || result.code == "QUOTA_EXCEEDED")
			{
				m_bootstrap.aiEnabled = false;
				m_bootstrap.aiDisabledReason = result.code;
				Publish();
				PublishAi();
				return;
			}
			if (!HandleCommonError(result))
			{
				m_lastAiSession = AiSessionState("offline");
				m_env->PublishAiSession(m_lastAiSession, 0);
			}
		});
	}
}
