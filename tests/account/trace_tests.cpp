/*
 * Market Canopy Office: traces of the account module against the P02 fixtures.
 * Copyright (C) 2026 Market Canopy LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// Each scenario drives CAccount through CFakeEnvironment: the credential
// store, the lock and the API are scripted, every request is checked against
// its fixture's request, and every answer is the fixture's response. The
// printed traces are the P05 evidence. After each scenario no published
// payload, log line or trace line may hold a refresh token, an access token,
// the sign-in code or the PKCE verifier.

#include <memory>

#include "account.h"
#include "crypto.h"
#include "fake_environment.h"

using namespace NSMarketCanopy;
using namespace NSTest;

namespace
{
	const char* const kRefresh1 = "dsr_EXAMPLE-not-a-real-refresh-token-0001";
	const char* const kRefresh2 = "dsr_EXAMPLE-not-a-real-refresh-token-0002";
	const char* const kOfficeToken =
		"eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhdWQiOiJvZmZpY2UiLCJleGFtcGxlIjp0cnVlfQ.ZXhhbXBsZS1zaWduYXR1cmUtbm90LXJlYWw";
	const char* const kConnectUrl = "https://app.marketcanopy.com/office/connect";

	std::string DesktopGrant()
	{
		return Fixture("credential-store/grant-valid.json").Get("input").Get("raw").AsString();
	}

	struct CRig
	{
		CFakeEnvironment env;
		std::unique_ptr<CAccount> account;

		CRig(bool installation, const std::string& grant)
		{
			env.Seed(installation, grant);
			CAccount::CConfig config;
			config.version = "10.0.0-mc.3";
			account.reset(new CAccount(&env, &env.store, &env.lock, config));
		}

		CGrant StoredGrant(const std::string& where)
		{
			std::string raw = env.store.Grant();
			CGrant grant;
			CHECK(ReadGrant(raw.empty() ? NULL : &raw, kApiOrigin, grant) == EGrantRead::Present, where + ": a grant is stored");
			return grant;
		}

		void SignInFromDesktop(const std::string& where)
		{
			env.Expect("linked-grant/refresh.json");
			env.Expect("bootstrap/ready.json");
			account->Start();
			env.Run(2000);
			CHECK_EQ(env.State(), std::string("signed_in"), where);
		}

		void Finish(const std::string& title, const std::vector<std::string>& secrets = std::vector<std::string>())
		{
			env.Done(title);
			std::vector<std::string> all = secrets;
			all.push_back(kFixtureCode);
			env.CheckNoSecrets(title, all);
			env.Print(title);
		}
	};

	std::string QueryValue(const std::string& url, const std::string& name)
	{
		size_t at = url.find(name + "=");
		if (at == std::string::npos)
			return std::string();
		at += name.size() + 1;
		size_t end = url.find('&', at);
		return url.substr(at, end == std::string::npos ? std::string::npos : end - at);
	}

	CRequestRule Omit(const char* header)
	{
		CRequestRule rule;
		rule.mayOmit.insert(header);
		return rule;
	}

	CRequestRule Differ(const char* header)
	{
		CRequestRule rule;
		rule.mayDiffer.insert(header);
		return rule;
	}

	// ---- 1. standalone sign-in (6.4) --------------------------------------

	void StandaloneSignIn()
	{
		const std::string title = "1. Standalone sign-in: system browser, PKCE, the callback forwarded to the running Office";
		CRig rig(true, "");
		CFakeEnvironment& env = rig.env;
		rig.account->Start();
		env.Run(1000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK_EQ(env.LastAi(), AiSessionState("signed_out"), title);
		CHECK(env.sent.empty(), title + ": no request without a grant");

		rig.account->SignIn();
		CHECK_EQ(env.State(), std::string("signing_in"), title);
		CHECK_EQ(env.browser.size(), (size_t)1, title);
		std::string url = env.browser.empty() ? std::string() : env.browser.back();
		CHECK(url.compare(0, std::string("https://app.marketcanopy.com/auth/native?clientApp=com.marketcanopy.office&codeChallenge=").size(),
						  "https://app.marketcanopy.com/auth/native?clientApp=com.marketcanopy.office&codeChallenge=") == 0,
			  title + ": the sign-in page on the app origin");
		std::string challenge = QueryValue(url, "codeChallenge");
		std::string state = QueryValue(url, "state");
		CHECK(IsBase64Url(challenge, 43, 43), title + ": S256 challenge");
		CHECK(IsBase64Url(state, 43, 43), title + ": 32-byte state");

		// a callback for some other sign-in is consumed and ignored
		std::string other = "mc-office://auth/callback?code=" + std::string(kFixtureCode) + "&state=ZXhhbXBsZS1zdGF0ZS1ub3QtcmVhbC0wMDAwMDAwMDE";
		CHECK(rig.account->HandleSchemeUrl(other), title);
		env.Run(500);
		CHECK_EQ(env.State(), std::string("signing_in"), title);
		CHECK(env.sent.empty(), title + ": nothing sent for a callback this Office did not start");

		std::string verifier;
		CRequestRule exchange;
		exchange.bodyExact = false;
		exchange.body = [&verifier, challenge, title](const Json& body) {
			CHECK_EQ(body.Get("code").AsString(), std::string(kFixtureCode), title);
			verifier = body.Get("codeVerifier").AsString();
			CHECK_EQ(verifier.size(), (size_t)86, title + ": 64-byte verifier");
			CHECK_EQ(PkceChallenge(verifier), challenge, title + ": the verifier matches the challenge in the browser URL");
			CHECK_EQ(body.Size(), (size_t)2, title + ": code and verifier only");
		};
		env.Expect("native-sign-in/valid-code.json", exchange);
		env.Expect("bootstrap/ready.json", Omit("x-active-organization-id"));
		std::string callback = "mc-office://auth/callback?code=" + std::string(kFixtureCode) + "&state=" + state;
		CHECK(rig.account->HandleSchemeUrl(callback), title);
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		CGrant grant = rig.StoredGrant(title);
		CHECK_EQ(grant.userId, std::string(kUserId), title);
		CHECK_EQ(grant.deviceSessionId, std::string(kDeviceSessionId), title);
		CHECK_EQ(grant.refreshToken, std::string(kRefresh1), title);
		CHECK_EQ(grant.writtenBy, std::string("office"), title);
		CHECK_EQ(grant.installationId, std::string(kInstallationId), title);
		CHECK_EQ(grant.lastActiveOrganizationId, std::string(kOrganizationId), title + ": the active organization is remembered");
		CHECK_EQ(env.providers.size(), (size_t)1, title + ": the start page lists Market Canopy as connected");
		Json state0 = env.LastState();
		CHECK_EQ(state0.Get("user").Get("email").AsString(), std::string("person@example.com"), title);

		// the same callback again does nothing
		size_t sent = env.sent.size();
		CHECK(rig.account->HandleSchemeUrl(callback), title);
		env.Run(500);
		CHECK_EQ(env.sent.size(), sent, title + ": a replayed callback is ignored");
		// a document link is left to the caller; a link that carries a credential is refused
		CHECK(!rig.account->HandleSchemeUrl(Fixture("deep-link/open.json").Get("input").AsString()), title);
		CHECK(rig.account->HandleSchemeUrl(Fixture("deep-link/credential-token-param.json").Get("input").AsString()), title);
		rig.Finish(title, std::vector<std::string>(1, verifier));
	}

	void SignInRefusals()
	{
		const std::string title = "1b. Standalone sign-in that does not finish: cancelled, timed out, refused, a second-factor answer";
		CRig rig(true, "");
		CFakeEnvironment& env = rig.env;
		rig.account->Start();
		env.Run(1000);

		rig.account->SignIn();
		rig.account->CancelSignIn();
		CHECK_EQ(env.State(), std::string("signed_out"), title + ": cancelled");

		rig.account->SignIn();
		env.Run(5 * 60 * 1000 + 2000);
		CHECK_EQ(env.State(), std::string("signed_out"), title + ": timed out");
		CHECK_EQ(env.LastState().Get("error").AsString(), std::string("SIGN_IN_TIMED_OUT"), title);

		rig.account->SignIn();
		std::string state = QueryValue(env.browser.back(), "state");
		CHECK(rig.account->HandleSchemeUrl("mc-office://auth/callback?error=ACCESS_DENIED&state=" + state), title);
		CHECK_EQ(env.LastState().Get("error").AsString(), std::string("ACCESS_DENIED"), title + ": the browser said no");

		rig.account->SignIn();
		state = QueryValue(env.browser.back(), "state");
		CRequestRule loose;
		loose.bodyExact = false;
		env.Expect("native-sign-in/replayed-code.json", loose);
		rig.account->HandleSchemeUrl("mc-office://auth/callback?code=" + std::string(kFixtureCode) + "&state=" + state);
		env.Run(1000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK_EQ(env.LastState().Get("error").AsString(), Fixture("native-sign-in/replayed-code.json").Get("expect").Get("outcome").AsString(),
				 title + ": a replayed code");

		// 1.1.0: a second-factor challenge is not an answer the exchange gives
		rig.account->SignIn();
		state = QueryValue(env.browser.back(), "state");
		env.ExpectPlain("POST", "/v1/auth/native/exchange", 200,
						Fixture("native-sign-in/exchange-no-second-factor.json").Get("input").Get("value").Dump(),
						"native-sign-in/exchange-no-second-factor.json, as the data of a 200");
		rig.account->HandleSchemeUrl("mc-office://auth/callback?code=" + std::string(kFixtureCode) + "&state=" + state);
		env.Run(1000);
		CHECK_EQ(env.LastState().Get("error").AsString(), std::string("UNEXPECTED_RESPONSE"), title);
		CHECK(env.store.Grant().empty(), title + ": nothing stored");
		env.Run(70000); // signed out, the 60-second re-read finds nothing
		rig.Finish(title);
	}

	// ---- 2. desktop-provisioned connection (5.4, 7.1) ------------------------

	void DesktopProvisioned()
	{
		const std::string title = "2. Desktop-provisioned connection: the desktop app writes the grant while Office is open";
		CRig rig(true, "");
		CFakeEnvironment& env = rig.env;
		rig.account->Start();
		env.Run(10000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);

		env.store.DesktopWrites(DesktopGrant());
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/ready.json");
		env.Run(60000); // the 60-second re-read while signed out (5.4 rule 5)
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		CGrant grant = rig.StoredGrant(title);
		CHECK_EQ(grant.refreshToken, std::string(kRefresh2), title + ": the rotated token is stored");
		CHECK_EQ(grant.writtenBy, std::string("office"), title);
		CHECK_EQ(grant.deviceSessionId, std::string(kDeviceSessionId), title + ": the desktop app's linked grant is kept");
		CHECK_EQ(env.providers.size(), (size_t)1, title);

		// the cloud connect page loaded in Office's own CEF: mint, then POST the code
		env.Expect("web-session/mint.json");
		CHECK(!rig.account->OnPageLoaded(7, "https://app.marketcanopy.com/office/documents"), title + ": another page");
		CHECK(rig.account->OnPageLoaded(7, kConnectUrl), title);
		env.Run(1000);
		CHECK_EQ(env.connectPosts.size(), (size_t)1, title);
		if (!env.connectPosts.empty())
		{
			CHECK_EQ(env.connectPosts[0].browserId, 7, title);
			CHECK_EQ(env.connectPosts[0].url, std::string(kConnectUrl), title + ": same origin, same page");
			CHECK_EQ(env.connectPosts[0].body, "code=" + std::string(kFixtureCode), title + ": the code is in the POST body");
		}
		// the page that could not connect gets one silent retry, then no more
		env.Expect("web-session/mint.json");
		CHECK(rig.account->OnPageLoaded(7, kConnectUrl), title + ": one retry");
		env.Run(1000);
		CHECK(!rig.account->OnPageLoaded(7, kConnectUrl), title + ": no third attempt");
		rig.Finish(title);
	}

	// ---- 3. rotation (5.4 rules 1 and 2) -------------------------------------

	void Rotation()
	{
		const std::string title = "3. Rotation: the access token runs out; the desktop app rotated meanwhile";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		CHECK_EQ(env.lock.acquisitions, 1, title + ": once, to rotate (the organization was already remembered)");

		env.Run(14 * 60 * 1000 + 30000);
		CHECK_EQ(env.sent.size(), (size_t)2, title + ": nothing is sent while the token is good");

		// the desktop app rotates the same grant (its own refresh, under the lock)
		CGrant desktop = rig.StoredGrant(title);
		desktop.refreshToken = "dsr_EXAMPLE-not-a-real-refresh-token-0003";
		desktop.writtenBy = "desktop";
		env.store.DesktopWrites(WriteGrant(desktop));

		// two connect pages at once: one rotation, two codes
		CRequestRule reread;
		reread.bodyExact = false;
		reread.body = [title](const Json& body) {
			CHECK_EQ(body.Get("refreshToken").AsString(), std::string("dsr_EXAMPLE-not-a-real-refresh-token-0003"),
					 title + ": the token re-read under the lock, not the one Office had");
		};
		std::vector<std::pair<std::string, std::string> > rotated;
		rotated.push_back(std::make_pair(std::string(kRefresh2), std::string("dsr_EXAMPLE-not-a-real-refresh-token-0004")));
		rotated.push_back(std::make_pair(std::string("ZXhhbXBsZS1zaWduYXR1cmUtbm90LXJlYWw"), std::string("cm90YXRlZC1zaWduYXR1cmUtbm90LXJlYWw")));
		env.Expect("linked-grant/refresh.json", reread, rotated);
		env.Expect("web-session/mint.json");
		env.Expect("web-session/mint.json");
		rig.account->OnPageLoaded(7, kConnectUrl);
		rig.account->OnPageLoaded(8, kConnectUrl);
		env.Run(2000);
		CHECK_EQ(env.connectPosts.size(), (size_t)2, title);
		CGrant grant = rig.StoredGrant(title);
		CHECK_EQ(grant.refreshToken, std::string("dsr_EXAMPLE-not-a-real-refresh-token-0004"), title);
		CHECK_EQ(grant.writtenBy, std::string("office"), title);
		CHECK(env.officeAccessToken != kOfficeToken, title + ": the new access token is the one in use");
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		rig.Finish(title, std::vector<std::string>(1, "cm90YXRlZC1zaWduYXR1cmUtbm90LXJlYWw"));
	}

	// ---- 4. revocation (6.3, 13) ---------------------------------------------

	void Revocation()
	{
		const std::string title = "4. Revocation: the refresh answers 401 after the desktop app signed out on the server";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		env.Run(15 * 60 * 1000);

		CRequestRule loose;
		loose.bodyExact = false;
		env.Expect("linked-grant/refresh-reused.json", loose);
		rig.account->OnPageLoaded(7, kConnectUrl);
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK_EQ(env.LastState().Get("error").AsString(), std::string("UNAUTHORIZED"), title);
		CHECK(env.store.Grant().empty(), title + ": compare-and-delete removed Office's revoked grant");
		CHECK_EQ(env.cleared.size(), (size_t)1, title + ": cookies and the provider session cleared");
		CHECK_EQ(env.LastAi(), AiSessionState("signed_out"), title + ": the AI plugins see signed_out");
		CHECK(env.connectPosts.empty(), title);
		rig.account->RequestAiSession();
		CHECK_EQ(env.LastAi(), AiSessionState("signed_out"), title);
		env.Run(70000);
		CHECK_EQ(env.State(), std::string("signed_out"), title + ": stays signed out");
		rig.Finish(title);
	}

	void SignedOutElsewhere()
	{
		const std::string title = "4b. Revocation seen locally: the desktop app deleted the grant; Office's window is activated";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		env.store.DesktopDeletes();
		rig.account->OnActivate();
		env.Run(1000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK_EQ(env.LastState().Get("error").AsString(), std::string("SIGNED_OUT_ELSEWHERE"), title);
		CHECK_EQ(env.cleared.size(), (size_t)1, title);
		CHECK_EQ(env.sent.size(), (size_t)2, title + ": nothing more is sent");
		rig.Finish(title);
	}

	void BootstrapRefused()
	{
		const std::string title = "4c. Revocation on an API call: the bootstrap answers 401, one rotation and retry, then signed out";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		CRequestRule loose;
		loose.bodyExact = false;
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/signed-out.json");
		env.Expect("linked-grant/refresh-reused.json", loose);
		rig.account->Start();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK(env.store.Grant().empty(), title);
		rig.Finish(title);
	}

	void SignOut()
	{
		const std::string title = "4d. Sign out from Office: logout revokes the session, then compare-and-delete";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		env.ExpectPlain("POST", "/v1/auth/logout", 200, "{}", "no fixture for /v1/auth/logout");
		rig.account->SignOut();
		env.Run(1000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK(env.store.Grant().empty(), title);
		CHECK_EQ(env.cleared.size(), (size_t)1, title);
		const CHttpRequest& logout = env.sent.back();
		bool bearer = false;
		for (size_t i = 0; i < logout.headers.size(); ++i)
			bearer = bearer || (logout.headers[i].first == "Authorization" && logout.headers[i].second == "Bearer " + env.officeAccessToken);
		CHECK(bearer, title + ": logout carries Office's own access token");
		rig.Finish(title);
	}

	void AccountChanged()
	{
		const std::string title = "4e. Account change: the desktop app signs in as another person";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		std::string other = Replace(Replace(DesktopGrant(), "0000000000a001", "0000000000a002"), "0000000000d001", "0000000000d002");
		env.store.DesktopWrites(other);
		std::vector<std::pair<std::string, std::string> > person;
		person.push_back(std::make_pair(std::string("0000000000a001"), std::string("0000000000a002")));
		person.push_back(std::make_pair(std::string("person@example.com"), std::string("other@example.com")));
		CRequestRule loose;
		loose.bodyExact = false;
		env.Expect("linked-grant/refresh.json", loose);
		env.Expect("bootstrap/ready.json", CRequestRule(), person);
		rig.account->OnActivate();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		CHECK_EQ(env.LastState().Get("user").Get("email").AsString(), std::string("other@example.com"), title);
		CHECK_EQ(env.cleared.size(), (size_t)1, title + ": the first person's web session and AI state are gone");
		CHECK_EQ(rig.StoredGrant(title).deviceSessionId, std::string("66f0c0ffee0000000000d002"), title);
		rig.Finish(title);
	}

	// ---- 5. wrong-GUID refusal (9) -------------------------------------------

	void WrongGuid()
	{
		const std::string title = "5. getMarketCanopyAiSession() callers: only the two system AI plugins";
#if defined(_WIN32)
		const std::wstring root = L"C:\\Program Files\\Market Canopy\\Office\\editors\\sdkjs-plugins";
		const std::wstring user = L"C:\\Users\\person\\AppData\\Local\\MarketCanopy\\Office\\data\\sdkjs-plugins";
#else
		const std::wstring root = L"/opt/marketcanopy/office/editors/sdkjs-plugins";
		const std::wstring user = L"/home/person/.local/share/marketcanopy/office/data/sdkjs-plugins";
#endif
		const std::wstring agent = L"{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}";
		const std::wstring editor = L"{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}";
		const std::wstring plugin = L"onlyoffice://plugin/";
		struct CCase
		{
			const char* what;
			std::wstring url;
			bool allowed;
		};
		std::vector<CCase> cases;
		cases.push_back(CCase{"start window agent", plugin + root + L"/" + agent + L"/index.html", true});
		cases.push_back(CCase{"in-editor AI plugin", plugin + root + L"/" + editor + L"/index.html?lang=en", true});
		cases.push_back(CCase{"another system plugin", plugin + root + L"/{B631D5D8-1B53-4C4A-8E7F-8E1D47A03D35}/index.html", false});
		cases.push_back(CCase{"the agent's GUID installed as a user plugin", plugin + user + L"/" + agent + L"/index.html", false});
		cases.push_back(CCase{"another page of the agent", plugin + root + L"/" + agent + L"/settings.html", false});
		cases.push_back(CCase{"a dot segment back into the agent", plugin + root + L"/{B631D5D8-1B53-4C4A-8E7F-8E1D47A03D35}/../" + agent + L"/index.html", false});
		cases.push_back(CCase{"an encoded dot segment", plugin + root + L"/%2E%2E/sdkjs-plugins/" + agent + L"/index.html", false});
		cases.push_back(CCase{"a web page", L"https://app.marketcanopy.com/" + agent + L"/index.html", false});
		cases.push_back(CCase{"the same file as file:///", L"file:///" + root + L"/" + agent + L"/index.html", false});
		cases.push_back(CCase{"no system plugins folder known", plugin + root + L"/" + agent + L"/index.html", false});

		CTrace trace;
		long long clock = StartMs();
		trace.clock = &clock;
		for (size_t i = 0; i < cases.size(); ++i)
		{
			bool last = i + 1 == cases.size();
			bool allowed = IsAiSessionFrame(cases[i].url, last ? std::wstring() : root);
			CHECK_EQ(allowed, cases[i].allowed, title + ": " + cases[i].what);
			trace.Add("renderer", std::string(cases[i].what) + ": " + (allowed ? "returns the session" : "undefined (method not exposed)"));
		}
		std::printf("\n=== %s\n", title.c_str());
		for (size_t i = 0; i < trace.lines.size(); ++i)
			std::printf("%s\n", trace.lines[i].c_str());
	}

	// ---- start page recognition ----------------------------------------------

	void StartPage()
	{
		const std::string title = "5b. mc:account: only Office's own start page drives the account";
#if defined(_WIN32)
		const std::wstring app = L"C:/Program Files/Market Canopy/Office";
		const std::wstring editors = L"C:\\Program Files\\Market Canopy\\Office/editors/web-apps/apps/api/documents/index.html";
		const std::wstring page = L"onlyoffice://plugin/C:/Program%20Files/Market%20Canopy/Office/index.html";
#else
		const std::wstring app = L"/opt/marketcanopy/office";
		const std::wstring editors = L"/opt/marketcanopy/office/editors/web-apps/apps/api/documents/index.html";
		const std::wstring page = L"onlyoffice://plugin//opt/marketcanopy/office/index.html";
#endif
		const std::wstring plugin = L"onlyoffice://plugin/";
		struct CCase
		{
			const char* what;
			std::wstring url;
			bool allowed;
		};
		std::vector<CCase> cases;
		cases.push_back(CCase{"the start page as Windows and Linux load it", page + L"?waitingloader=yes&lang=en", true});
		cases.push_back(CCase{"the start page as macOS loads it", L"file:///Applications/MarketCanopyOffice.app/Contents/Resources/index.html", true});
		cases.push_back(CCase{"another page in the application folder", plugin + app + L"/other.html", false});
		cases.push_back(CCase{"the editors' page", plugin + editors, false});
		cases.push_back(CCase{"a system plugin page", plugin + app + L"/editors/sdkjs-plugins/{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}/index.html", false});
		cases.push_back(CCase{"a dot segment back to the start page", plugin + app + L"/editors/../index.html", false});
		cases.push_back(CCase{"a web page", L"https://app.marketcanopy.com/index.html", false});

		for (size_t i = 0; i < cases.size(); ++i)
			CHECK_EQ(IsStartPageUrl(cases[i].url, editors), cases[i].allowed, title + ": " + cases[i].what);
		CHECK_EQ(IsStartPageUrl(page, std::wstring()), false, title + ": no editors path known");
		std::printf("\n=== %s\n", title.c_str());
	}

	// ---- AI session (8.2, 9) ---------------------------------------------------

	void AiSession()
	{
		const std::string title = "6. AI session: minted for the plugins, then the plan without AI";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		rig.SignInFromDesktop(title);
		env.Expect("ai-session/token.json");
		rig.account->RequestAiSession();
		env.Run(1000);
		Json published;
		CHECK(Json::Parse(env.LastAi(), published), title);
		CheckSame(published, Fixture("ai-session/native-ready.json").Get("input").Get("value"), title + ": the fixture's ready result");
		CHECK_EQ(env.aiSessions.back().second, ParseInstant("2026-10-24T21:55:00.000Z"), title);

		CRig plan(true, DesktopGrant());
		plan.SignInFromDesktop(title);
		plan.env.Expect("ai-session/token-not-entitled.json");
		plan.account->RequestAiSession();
		plan.env.Run(1000);
		CheckSame([&]() { Json v; Json::Parse(plan.env.LastAi(), v); return v; }(),
				  Fixture("ai-session/native-not-entitled.json").Get("input").Get("value"), title);
		CHECK_EQ(plan.env.LastState().Get("ai").Get("disabledReason").AsString(), std::string("AI_NOT_ENTITLED"), title);
		env.Print(title);
		plan.Finish(title + " (no AI in the plan)");
		env.Done(title);
	}

	// ---- update required, organizations, memory-only, offline -------------------

	void UpdateRequired()
	{
		const std::string title = "7. Update required: below minimumOfficeVersion only cloud and AI turn off";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/update-required.json");
		rig.account->Start();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("update_required"), title);
		CHECK_EQ(env.LastAi(), AiSessionState("update_required"), title);
		CHECK(env.providers.empty(), title + ": the cloud is not offered");
		CHECK(!rig.account->OnPageLoaded(7, kConnectUrl), title);
		rig.Finish(title);

		CRig outdated(true, DesktopGrant());
		outdated.env.Expect("linked-grant/refresh-outdated.json", Differ("X-Client-Version")); // the fixture's client is older
		outdated.account->Start();
		outdated.env.Run(2000);
		CHECK_EQ(outdated.env.State(), std::string("update_required"), title);
		CHECK_EQ(outdated.env.LastState().Get("update").Get("storeUrl").AsString(), std::string("https://app.marketcanopy.com/download/office"),
				 title + ": 426 details.storeUrl");
		CHECK(!outdated.StoredGrant(title).refreshToken.empty(), title + ": the grant stays for after the update");
		outdated.Finish(title + " (426 from the refresh)");
	}

	void Organizations()
	{
		const std::string title = "8. Organizations: the remembered one is gone, then the person picks one";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/not-a-member.json");
		env.Expect("bootstrap/no-organization.json");
		rig.account->Start();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("no_organization"), title);
		CHECK_EQ(env.LastAi(), AiSessionState("not_entitled"), title);

		std::vector<std::pair<std::string, std::string> > picked;
		picked.push_back(std::make_pair(std::string("\"activeOrganizationId\":\"66f0c0ffee0000000000b001\""),
										std::string("\"activeOrganizationId\":\"66f0c0ffee0000000000b002\"")));
		env.Expect("bootstrap/ready.json", Differ("x-active-organization-id"), picked);
		rig.account->SelectOrganization("66f0c0ffee0000000000b002");
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		CHECK_EQ(env.LastState().Get("activeOrganizationId").AsString(), std::string("66f0c0ffee0000000000b002"), title);
		CHECK_EQ(rig.StoredGrant(title).lastActiveOrganizationId, std::string("66f0c0ffee0000000000b002"), title);
		CHECK_EQ(env.cleared.size(), (size_t)1, title + ": the web session was bound to the old organization");
		rig.Finish(title);
	}

	void MemoryOnly()
	{
		const std::string title = "9. No secure store: a memory-only session, nothing written anywhere";
		CRig rig(false, "");
		CFakeEnvironment& env = rig.env;
		env.store.unavailable = true;
		rig.account->Start();
		env.Run(1000);
		CHECK_EQ(env.State(), std::string("signed_out"), title);
		CHECK_EQ(env.LastState().Get("credentialStore").AsString(), std::string("unavailable"), title);
		rig.account->SignIn();
		std::string state = QueryValue(env.browser.back(), "state");
		CRequestRule exchange;
		exchange.bodyExact = false;
		exchange.mayDiffer.insert("X-Device-Id"); // a session-only installation id
		CRequestRule bootstrap = exchange;
		bootstrap.mayOmit.insert("x-active-organization-id");
		env.Expect("native-sign-in/valid-code.json", exchange);
		env.Expect("bootstrap/ready.json", bootstrap);
		env.Expect("linked-grant/refresh.json", exchange);
		env.Expect("web-session/mint.json", exchange);
		rig.account->HandleSchemeUrl("mc-office://auth/callback?code=" + std::string(kFixtureCode) + "&state=" + state);
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		env.Run(15 * 60 * 1000);
		rig.account->OnPageLoaded(3, kConnectUrl);
		env.Run(2000);
		CHECK_EQ(env.lock.acquisitions, 0, title + ": no lock without a store");
		CHECK(env.store.entries.empty(), title);
		rig.Finish(title);
	}

	void Offline()
	{
		const std::string title = "10. Offline at launch, connected on the next 60-second try";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		env.ExpectOffline("POST", "/v1/auth/refresh");
		rig.account->Start();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("offline"), title);
		CHECK_EQ(env.LastAi(), AiSessionState("offline"), title);
		CHECK_EQ(rig.StoredGrant(title).refreshToken, std::string(kRefresh1), title + ": offline keeps the grant");
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/ready.json");
		env.Run(60000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		rig.Finish(title);
	}

	void LockBusy()
	{
		const std::string title = "11. The desktop app holds the lock for 30 seconds: Office waits, then tries again";
		CRig rig(true, DesktopGrant());
		CFakeEnvironment& env = rig.env;
		env.lock.heldByDesktop = true;
		rig.account->Start();
		env.Run(2000);
		CHECK_EQ(env.State(), std::string("failed"), title);
		CHECK(env.sent.empty(), title + ": no refresh without the lock");
		env.lock.heldByDesktop = false;
		env.Expect("linked-grant/refresh.json");
		env.Expect("bootstrap/ready.json");
		env.Run(60000);
		CHECK_EQ(env.State(), std::string("signed_in"), title);
		rig.Finish(title);
	}
}

int main()
{
	StandaloneSignIn();
	SignInRefusals();
	DesktopProvisioned();
	Rotation();
	Revocation();
	SignedOutElsewhere();
	BootstrapRefused();
	SignOut();
	AccountChanged();
	WrongGuid();
	StartPage();
	AiSession();
	UpdateRequired();
	Organizations();
	MemoryOnly();
	Offline();
	LockBusy();
	std::printf("\n");
	return Finish("trace_tests");
}
