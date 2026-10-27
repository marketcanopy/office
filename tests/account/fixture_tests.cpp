/*
 * Market Canopy Office: the native contract readers against the P02 fixtures.
 * Copyright (C) 2026 Market Canopy LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// Every fixture whose subject the account module implements natively, with
// the same expectation contracts/test/fixtures.test.ts checks the TypeScript
// reference against. Subjects that belong to other units (launch arguments,
// recents, document locks, the gateway stream, install manifests, the
// provider config page) are counted and left to them.

#include <map>

#include "contract.h"
#include "test_support.h"

using namespace NSMarketCanopy;
using namespace NSTest;

namespace
{
	std::map<std::string, int> g_ran;
	std::map<std::string, int> g_left;

	Json Str(const std::string& value) { return Json::MakeString(value); }

	Json LinkJson(const CSchemeLink& link)
	{
		Json value = Json::MakeObject();
		switch (link.kind)
		{
		case CSchemeLink::Invalid:
		case CSchemeLink::CredentialInLink:
			value.Set("ok", Json::MakeBool(false));
			value.Set("error", Str(link.kind == CSchemeLink::Invalid ? "INVALID_LINK" : "CREDENTIAL_IN_LINK"));
			break;
		case CSchemeLink::Open:
			value.Set("ok", Json::MakeBool(true));
			value.Set("kind", Str("open"));
			value.Set("name", Str(link.name));
			value.Set("appOrigin", Str(link.appOrigin));
			value.Set("documentId", Str(link.documentId));
			value.Set("organizationId", Str(link.organizationId));
			break;
		case CSchemeLink::AuthCallback:
			value.Set("ok", Json::MakeBool(true));
			value.Set("kind", Str("auth_callback"));
			value.Set("code", Str(link.code));
			value.Set("state", Str(link.state));
			break;
		case CSchemeLink::AuthError:
			value.Set("ok", Json::MakeBool(true));
			value.Set("kind", Str("auth_error"));
			value.Set("error", Str(link.error));
			value.Set("state", Str(link.state));
			break;
		}
		return value;
	}

	Json GrantJson(const CGrant& grant)
	{
		Json value = Json::MakeObject();
		value.Set("version", Json::MakeNumber(1));
		value.Set("apiOrigin", Str(grant.apiOrigin));
		value.Set("userId", Str(grant.userId));
		value.Set("deviceSessionId", Str(grant.deviceSessionId));
		value.Set("installationId", Str(grant.installationId));
		value.Set("refreshToken", Str(grant.refreshToken));
		value.Set("issuedAt", Str(grant.issuedAt));
		value.Set("writtenBy", Str(grant.writtenBy));
		value.Set("lastActiveOrganizationId", grant.lastActiveOrganizationId.empty() ? Json() : Str(grant.lastActiveOrganizationId));
		return value;
	}

	void CredentialRead(const std::string& where, const Json& input, const Json& expect)
	{
		std::string raw = input.Get("raw").IsString() ? input.Get("raw").AsString() : std::string();
		CGrant grant;
		EGrantRead read = ReadGrant(input.Get("raw").IsString() ? &raw : NULL, input.Get("apiOrigin").AsString(), grant);
		Json actual = Json::MakeObject();
		if (read == EGrantRead::Present)
		{
			actual.Set("state", Str("present"));
			actual.Set("value", GrantJson(grant));
			// a value Office reads back is the value it writes
			CGrant again;
			std::string written = WriteGrant(grant);
			CHECK(ReadGrant(&written, input.Get("apiOrigin").AsString(), again) == EGrantRead::Present, where);
			CheckSame(GrantJson(again), GrantJson(grant), where + " round trip");
		}
		else
		{
			actual.Set("state", Str("absent"));
			actual.Set("reason", Str(GrantReadName(read)));
		}
		if (input.Has("signedInUserId"))
			actual.Set("desktopMayWrite", Json::MakeBool(DesktopMayWrite(read, grant, input.Get("signedInUserId").AsString())));
		if (input.Has("revokedDeviceSessionId"))
			actual.Set("mayDelete", Json::MakeBool(MayDelete(read, grant, input.Get("revokedDeviceSessionId").AsString())));
		CheckSame(actual, expect, where);
	}

	void ClientVersion(const std::string& where, const Json& input, const Json& expect)
	{
		if (input.Has("release"))
		{
			std::string header;
			bool ok = ClientVersionHeader(input.Get("release").AsString(), header);
			Json actual = Json::MakeObject();
			actual.Set("header", ok ? Str(header) : Json());
			CheckSame(actual, expect, where);
			return;
		}
		int result = CompareVersions(input.Get("compare").At(0).AsString(), input.Get("compare").At(1).AsString());
		Json actual = Json::MakeObject();
		actual.Set("result", result == 2 ? Json() : Json::MakeNumber(result));
		CheckSame(actual, expect, where);
	}

	// What the native method returns for the state the fixture shows.
	Json NativeAiSession(const Json& value)
	{
		std::string state = value.Get("state").AsString();
		std::string text;
		if (state == "ready")
		{
			std::vector<CModel> models;
			const Json& list = value.Get("models");
			for (size_t i = 0; i < list.Size(); ++i)
			{
				CModel model;
				model.id = list.At(i).Get("id").AsString();
				model.name = list.At(i).Get("name").AsString();
				for (size_t k = 0; k < list.At(i).Get("capabilities").Size(); ++k)
					model.capabilities.push_back(list.At(i).Get("capabilities").At(k).AsString());
				models.push_back(model);
			}
			text = AiSessionReady(value.Get("baseUrl").AsString(), value.Get("accessToken").AsString(), value.Get("expiresAt").AsString(),
								  value.Get("organizationId").AsString(), models, value.Get("defaultModel").AsString());
		}
		else
			text = AiSessionState(state.c_str());
		Json native;
		Json::Parse(text, native);
		return native;
	}

	void Document(const std::string& where, const std::string& folder, const Json& input, const Json& expect)
	{
		std::string schema = input.Get("schema").AsString();
		const Json& value = input.Get("value");
		bool valid = expect.Get("valid").AsBool();
		if (schema == "credential-installation")
		{
			std::string id;
			CHECK_EQ(ReadInstallation(value.Dump(), id), valid, where);
		}
		else if (schema == "credential-grant")
		{
			std::string raw = value.Dump();
			CGrant grant;
			CHECK_EQ(ReadGrant(&raw, value.Get("apiOrigin").AsString(), grant) == EGrantRead::Present, valid, where);
		}
		else if (schema == "ai-session")
			// a valid result is exactly what the native method produces; an invalid one never is
			CHECK_EQ(Same(NativeAiSession(value), value), valid, where);
		else if (schema == "bootstrap")
		{
			// The two invalid bootstraps turn AI on where 8.1 says it is off. The
			// reader refuses them, or reads them with AI (and, below the minimum
			// version, docs) off: it never turns on what must be off.
			CBootstrap bootstrap;
			bool read = ReadBootstrap(value, bootstrap);
			if (valid)
				CHECK(read, where);
			else
				CHECK(!read || (!bootstrap.aiEnabled && !(bootstrap.updateRequired && bootstrap.docsEnabled)), where);
			// 2.3.0: an organization's https logo is kept, anything else is dropped
			const Json& organizations = value.Get("organizations");
			for (size_t i = 0; valid && read && i < organizations.Size() && i < bootstrap.organizations.size(); ++i)
			{
				const Json& logo = organizations.At(i).Get("logoUrl");
				CHECK_EQ(bootstrap.organizations[i].logoUrl, logo.IsString() ? logo.AsString() : std::string(), where + " logoUrl");
				if (!logo.IsString())
					continue;
				const char* refused[] = {"http://files.example.com/logo.png", "javascript:alert(1)", "https://files.example.com/a b.png",
										 "https://files.example.com/\"onerror=\".png"};
				for (size_t r = 0; r < sizeof(refused) / sizeof(refused[0]); ++r)
				{
					std::string text = value.Dump();
					size_t at = text.find(Json::Quote(logo.AsString()));
					text.replace(at, Json::Quote(logo.AsString()).size(), Json::Quote(refused[r]));
					Json changed;
					CBootstrap other;
					CHECK(Json::Parse(text, changed) && ReadBootstrap(changed, other), where + " refused logo still reads");
					CHECK_EQ(other.organizations[i].logoUrl, std::string(), where + " refused logo: " + refused[r]);
				}
			}
		}
		else
		{
			++g_left[folder + " (" + schema + ")"];
			return;
		}
		++g_ran[folder];
	}

	void Exchange(const std::string& where, const std::string& folder, const Json& input, const Json& expect)
	{
		const Json& response = input.Get("response");
		std::string path = input.Get("request").Get("path").AsString();
		if (path.compare(0, 14, "/v1/office/ai/") == 0)
		{
			++g_left[folder + " (gateway exchange)"];
			return;
		}
		int status = (int)response.Get("status").AsNumber();
		CApiResult result = ReadEnvelope(status, response.Get("body").Dump(), false);
		std::string outcome = expect.Get("outcome").AsString();
		CHECK_EQ(result.status, status, where);
		if (outcome == "ok")
		{
			CHECK(result.kind == CApiResult::Ok, where);
			if (response.Get("schema").IsString() && response.Get("schema").AsString() == "bootstrap")
			{
				CBootstrap bootstrap;
				CHECK(ReadBootstrap(result.data, bootstrap), where + " bootstrap");
			}
		}
		else
		{
			CHECK(result.kind == CApiResult::Error, where);
			CHECK_EQ(result.code, outcome, where);
			if (outcome == "CLIENT_UPGRADE_REQUIRED")
				CHECK(result.details.Get("storeUrl").IsString() && result.details.Get("minSupportedVersion").IsString(), where);
		}
		// the version Office sends is the header form of its release (6.1)
		const Json& version = input.Get("request").Get("headers").Get("X-Client-Version");
		if (version.IsString() && version.AsString() == "10.0.0.3")
		{
			std::string header;
			CHECK(ClientVersionHeader("10.0.0-mc.3", header) && header == "10.0.0.3", where);
		}
		++g_ran[folder];
	}
}

int main()
{
	const char* folders[] = {"ai-session", "bootstrap", "client-version", "credential-store", "deep-link", "gateway", "install-manifest",
							 "launch", "linked-grant", "locks", "native-sign-in", "recents", "web-session"};
	for (size_t f = 0; f < sizeof(folders) / sizeof(folders[0]); ++f)
	{
		std::string folder = folders[f];
		std::vector<std::string> files = FixtureFiles(folder);
		CHECK(!files.empty() || folder == "locks", "fixtures in " + folder);
		for (size_t i = 0; i < files.size(); ++i)
		{
			if (files[i].size() < 5 || files[i].compare(files[i].size() - 5, 5, ".json") != 0)
				continue;
			Json fixture = Fixture(files[i]);
			std::string subject = fixture.Get("subject").AsString();
			const Json& input = fixture.Get("input");
			const Json& expect = fixture.Get("expect");
			const std::string& where = files[i];
			if (subject == "deep-link")
			{
				std::string url = input.AsString();
				CheckSame(LinkJson(ParseSchemeUrl(url)), expect, where);
				++g_ran[folder];
			}
			else if (subject == "credential-read")
			{
				CredentialRead(where, input, expect);
				++g_ran[folder];
			}
			else if (subject == "client-version")
			{
				ClientVersion(where, input, expect);
				++g_ran[folder];
			}
			else if (subject == "document")
				Document(where, folder, input, expect);
			else if (subject == "exchange")
				Exchange(where, folder, input, expect);
			else
				++g_left[folder + " (" + subject + ")"];
		}
	}

	int ran = 0;
	std::printf("fixtures run against the native readers:\n");
	for (std::map<std::string, int>::const_iterator it = g_ran.begin(); it != g_ran.end(); ++it)
	{
		std::printf("  %-18s %d\n", it->first.c_str(), it->second);
		ran += it->second;
	}
	std::printf("  %-18s %d\n", "total", ran);
	std::printf("left to the units that implement them:\n");
	for (std::map<std::string, int>::const_iterator it = g_left.begin(); it != g_left.end(); ++it)
		std::printf("  %s: %d\n", it->first.c_str(), it->second);
	return Finish("fixture_tests");
}
