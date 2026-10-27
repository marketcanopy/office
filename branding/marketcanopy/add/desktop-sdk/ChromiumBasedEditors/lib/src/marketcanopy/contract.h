/*
 * Market Canopy Office: the integration contract's values, readers and writers.
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

// Native counterpart of contracts/src/ (the TypeScript reference). Section
// numbers are contracts/SPEC.md's. tests/account runs every fixture these
// functions have a reference for.

#ifndef MARKETCANOPY_CONTRACT_H
#define MARKETCANOPY_CONTRACT_H

#include <string>
#include <vector>

#include "json.h"

namespace NSMarketCanopy
{
	// 2. Identity values
	extern const char* const kClientApp;          // X-Client-App
	extern const char* const kScheme;             // the URL scheme, without "://"
	extern const char* const kProviderId;         // the cloud provider id
	extern const char* const kInstallationAccount;
	extern const char* const kAgentPluginGuid;    // start window agent
	extern const char* const kEditorAiPluginGuid; // in-editor AI plugin
	const size_t kMaxGrantBytes = 2048;

	struct COrigins
	{
		std::string api;
		std::string app;
	};

	const char* ServiceName();
	COrigins Origins();
	std::string GrantAccount(const std::string& apiOrigin);

	bool IsObjectId(const std::string& value);
	bool IsUuidV4(const std::string& value);
	bool IsBase64Url(const std::string& value, size_t minLength, size_t maxLength);
	// ISO 8601 instant as JavaScript writes it (2026-10-24T21:40:00.000Z, an
	// offset instead of Z is accepted); milliseconds since 1970, or -1.
	long long ParseInstant(const std::string& value);
	std::string FormatInstant(long long epochMs);

	// 5.2 Grant value, version 1
	struct CGrant
	{
		std::string apiOrigin;
		std::string userId;
		std::string deviceSessionId;
		std::string installationId;
		std::string refreshToken;
		std::string issuedAt;
		std::string writtenBy;                // "desktop" or "office"
		std::string lastActiveOrganizationId; // empty is JSON null
	};

	enum class EGrantRead { Present, Missing, Unreadable, OtherVersion, TooLarge, Invalid };
	const char* GrantReadName(EGrantRead value);

	// `raw` is NULL when the entry does not exist.
	EGrantRead ReadGrant(const std::string* raw, const std::string& apiOrigin, CGrant& out);
	std::string WriteGrant(const CGrant& grant);
	// 5.4 rules 3 and 4, shared with the desktop app
	bool DesktopMayWrite(EGrantRead state, const CGrant& current, const std::string& signedInUserId);
	bool MayDelete(EGrantRead state, const CGrant& current, const std::string& revokedDeviceSessionId);

	// 5.1 installation entry
	bool ReadInstallation(const std::string& raw, std::string& installationId);
	std::string WriteInstallation(const std::string& installationId);

	// 4. URL scheme
	struct CSchemeLink
	{
		enum EKind { Invalid, CredentialInLink, Open, AuthCallback, AuthError };
		EKind kind;
		std::string name;
		std::string appOrigin;
		std::string documentId;
		std::string organizationId;
		std::string code;
		std::string state;
		std::string error;
		CSchemeLink() : kind(Invalid) {}
	};
	CSchemeLink ParseSchemeUrl(const std::string& input);
	bool IsSchemeUrl(const std::string& input); // starts with "mc-office:" (any case)

	// 6.1 client version
	bool ClientVersionHeader(const std::string& release, std::string& header);
	// -1, 0, 1, or 2 when either is malformed
	int CompareVersions(const std::string& left, const std::string& right);

	// 6. API envelope
	struct CApiResult
	{
		enum EKind { Ok, Error, Network };
		EKind kind;
		int status;
		Json data;
		std::string code;    // error code, "NETWORK_UNAVAILABLE" for Network
		std::string message; // safe text from the API
		Json details;
		CApiResult() : kind(Network), status(0) {}
	};
	CApiResult ReadEnvelope(int status, const std::string& body, bool networkError);

	// 8.1 bootstrap
	struct CModel
	{
		std::string id;
		std::string name;
		std::vector<std::string> capabilities;
	};
	struct COrganization
	{
		std::string id;
		std::string name;
		std::string logoUrl; // 2.3.0, empty when none or not an https URL
	};
	struct CBootstrap
	{
		std::string userId;
		std::string email;
		std::string displayName;
		std::vector<COrganization> organizations;
		std::string activeOrganizationId;
		std::string minimumVersion;
		std::string latestVersion;
		bool updateRequired;
		bool docsEnabled;
		std::string docsAppOrigin;
		std::string connectPath;
		std::string documentsPath;
		bool aiEnabled;
		std::string aiDisabledReason;
		std::string aiBaseUrl;
		std::vector<CModel> models;
		std::string defaultModel;
		CBootstrap() : updateRequired(false), docsEnabled(false), aiEnabled(false) {}
	};
	bool ReadBootstrap(const Json& data, CBootstrap& out);

	// 9. getMarketCanopyAiSession() results
	std::string AiSessionReady(const std::string& baseUrl, const std::string& accessToken, const std::string& expiresAt,
							   const std::string& organizationId, const std::vector<CModel>& models,
							   const std::string& defaultModel);
	std::string AiSessionState(const char* state); // signed_out, offline, update_required, not_entitled

	// 9. the frames that may call the AI session method: system plugins only,
	// through the plugin scheme (start window) or a file URL (editors)
	bool IsAiSessionFrame(const std::wstring& frameUrl, const std::wstring& systemPluginsDir);

	// The start page, which alone drives the account (mc:account) and hears
	// its state: localEditorsPath is the manager's local_editors_path setting.
	bool IsStartPageUrl(const std::wstring& url, const std::wstring& localEditorsPath);
}

#endif // MARKETCANOPY_CONTRACT_H
