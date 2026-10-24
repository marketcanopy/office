/*
 * Market Canopy Office: a scripted IEnvironment, credential store and lock
 * for the account module traces.
 * Copyright (C) 2026 Market Canopy LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

// Time is virtual. Store and lock calls run as queued tasks, so the "desktop
// app" in a test can change the entry between them. Each HTTP request must
// be the next one the test expects; its answer comes from a P02 fixture and
// arrives 40 ms later. Everything that happens is written to a trace that
// never holds a token, a code or a verifier.

#ifndef MARKETCANOPY_FAKE_ENVIRONMENT_H
#define MARKETCANOPY_FAKE_ENVIRONMENT_H

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>

#include "contract.h"
#include "platform.h"
#include "test_support.h"

namespace NSTest
{
	using namespace NSMarketCanopy;

	const char* const kApiOrigin = "https://api.marketcanopy.com";
	const char* const kAppOrigin = "https://app.marketcanopy.com";
	const char* const kInstallationId = "4f7d2c1e-8a8b-4c39-9d7e-2b1f5a0c9e11";
	const char* const kUserId = "66f0c0ffee0000000000a001";
	const char* const kDeviceSessionId = "66f0c0ffee0000000000d001";
	const char* const kOrganizationId = "66f0c0ffee0000000000b001";
	const char* const kFixtureCode = "ZXhhbXBsZS1jb2RlLW5vdC1yZWFsLTAwMDAwMDAwMDE";

	inline long long StartMs() { return ParseInstant("2026-10-24T21:40:00.000Z"); }

	class CTrace
	{
	public:
		long long* clock;
		std::vector<std::string> lines;
		CTrace() : clock(NULL) {}
		void Add(const std::string& who, const std::string& what)
		{
			char stamp[32];
			std::snprintf(stamp, sizeof(stamp), "+%8.3fs", (double)(*clock - StartMs()) / 1000.0);
			std::string who8 = who;
			who8.resize(8, ' ');
			lines.push_back(std::string(stamp) + "  " + who8 + " " + what);
		}
	};

	// Names a grant by what it is, never by its token.
	inline std::string Describe(const std::string& raw)
	{
		CGrant grant;
		EGrantRead read = ReadGrant(&raw, kApiOrigin, grant);
		if (read != EGrantRead::Present)
			return GrantReadName(read);
		std::string token = grant.refreshToken.size() >= 4 ? grant.refreshToken.substr(grant.refreshToken.size() - 4) : "";
		return "user " + grant.userId.substr(20) + ", session " + grant.deviceSessionId.substr(20) + ", refresh token #" + token +
			   ", written by " + grant.writtenBy + ", organization " +
			   (grant.lastActiveOrganizationId.empty() ? std::string("none") : grant.lastActiveOrganizationId.substr(20));
	}

	class CMemoryLock : public IStoreLock
	{
	public:
		CTrace* trace;
		bool held;
		bool heldByDesktop;
		int acquisitions;
		CMemoryLock() : trace(NULL), held(false), heldByDesktop(false), acquisitions(0) {}
		bool Acquire(int) override
		{
			if (held)
				Fail("lock", "acquired again while Office holds it (the real lock would wait 30 seconds)");
			if (heldByDesktop)
			{
				trace->Add("lock", "busy for 30 seconds (the desktop app holds it)");
				return false;
			}
			held = true;
			++acquisitions;
			trace->Add("lock", "acquired");
			return true;
		}
		void Release() override
		{
			if (!held)
				Fail("lock", "released while not held");
			held = false;
			trace->Add("lock", "released");
		}
	};

	class CMemoryStore : public ICredentialStore
	{
	public:
		CTrace* trace;
		CMemoryLock* lock;
		bool unavailable;
		std::map<std::string, std::string> entries;
		CMemoryStore() : trace(NULL), lock(NULL), unavailable(false) {}

		static std::string Short(const std::string& account) { return account == kInstallationAccount ? "installation" : "grant"; }

		EStatus Read(const std::string& account, std::string& value) override
		{
			if (unavailable)
			{
				trace->Add("store", "read " + Short(account) + ": no secure store");
				return Unavailable;
			}
			std::map<std::string, std::string>::const_iterator it = entries.find(account);
			if (it == entries.end())
			{
				trace->Add("store", "read " + Short(account) + ": missing");
				return NotFound;
			}
			value = it->second;
			trace->Add("store", "read " + Short(account) + (account == kInstallationAccount ? std::string() : ": " + Describe(value)));
			return Ok;
		}
		EStatus Write(const std::string& account, const std::string& value) override
		{
			if (unavailable)
				return Unavailable;
			if (!lock->held)
				Fail("store", "wrote " + Short(account) + " without holding the lock (5.4 rule 2)");
			entries[account] = value;
			trace->Add("store", "write " + Short(account) + (account == kInstallationAccount ? std::string() : ": " + Describe(value)));
			return Ok;
		}
		EStatus Delete(const std::string& account) override
		{
			if (unavailable)
				return Unavailable;
			if (!lock->held)
				Fail("store", "deleted " + Short(account) + " without holding the lock (5.4 rule 4)");
			entries.erase(account);
			trace->Add("store", "delete " + Short(account));
			return Ok;
		}

		// The desktop app, writing as P06 does (under its own lock, which is free here).
		void DesktopWrites(const std::string& raw)
		{
			entries[GrantAccount(kApiOrigin)] = raw;
			trace->Add("desktop", "writes the grant: " + Describe(raw));
		}
		void DesktopDeletes()
		{
			entries.erase(GrantAccount(kApiOrigin));
			trace->Add("desktop", "signs out and deletes the grant");
		}
		std::string Grant() const
		{
			std::map<std::string, std::string>::const_iterator it = entries.find(GrantAccount(kApiOrigin));
			return it == entries.end() ? std::string() : it->second;
		}
	};

	struct CRequestRule
	{
		bool bodyExact;                       // the body equals the fixture's
		std::set<std::string> mayOmit;        // fixture headers Office may leave out here
		std::set<std::string> mayDiffer;      // fixture headers whose value is this scenario's own
		std::function<void(const Json&)> body; // extra checks on the body
		CRequestRule() : bodyExact(true) {}
	};

	struct CExpected
	{
		std::string fixture;
		std::string method;
		std::string path;
		Json request; // the fixture's request
		CHttpResponse response;
		CRequestRule rule;
	};

	class CFakeEnvironment : public IEnvironment
	{
	public:
		long long now;
		CTrace trace;
		CMemoryLock lock;
		CMemoryStore store;

		std::deque<CExpected> expected;
		std::vector<CHttpRequest> sent;
		std::vector<std::string> states;
		std::vector<std::string> providers;
		std::vector<std::string> cleared;
		std::vector<std::pair<std::string, long long> > aiSessions;
		std::vector<std::string> browser;
		struct CConnectPost { int browserId; std::string url; std::string body; };
		std::vector<CConnectPost> connectPosts;
		std::vector<std::string> logs;
		std::string officeAccessToken; // the last one an auth route issued

		CFakeEnvironment() : now(StartMs()), m_seq(0)
		{
			trace.clock = &now;
			lock.trace = &trace;
			store.trace = &trace;
			store.lock = &lock;
		}

		// ---- the test's side ------------------------------------------------

		void Seed(bool withInstallation, const std::string& grantRaw)
		{
			if (withInstallation)
				store.entries[kInstallationAccount] = WriteInstallation(kInstallationId);
			if (!grantRaw.empty())
				store.entries[GrantAccount(kApiOrigin)] = grantRaw;
		}

		// The next request is the fixture's; `edits` are applied to its response body text.
		void Expect(const std::string& fixture, const CRequestRule& rule = CRequestRule(),
					const std::vector<std::pair<std::string, std::string> >& edits = std::vector<std::pair<std::string, std::string> >())
		{
			Json value = Fixture(fixture);
			CExpected next;
			next.fixture = fixture;
			next.request = value.Get("input").Get("request");
			next.method = next.request.Get("method").AsString();
			next.path = next.request.Get("path").AsString();
			const Json& response = value.Get("input").Get("response");
			next.response.networkError = false;
			next.response.status = (int)response.Get("status").AsNumber();
			std::string body = response.Get("body").Dump();
			for (size_t i = 0; i < edits.size(); ++i)
				body = Replace(body, edits[i].first, edits[i].second);
			next.response.body = body;
			next.rule = rule;
			expected.push_back(next);
		}

		// A request answered with no HTTP response at all.
		void ExpectOffline(const std::string& method, const std::string& path)
		{
			CExpected next;
			next.fixture = "(no answer: offline)";
			next.method = method;
			next.path = path;
			expected.push_back(next);
		}

		// A request no exchange fixture describes, answered with an envelope around `data`.
		void ExpectPlain(const std::string& method, const std::string& path, int status, const std::string& data,
						 const std::string& label = "no fixture for this route")
		{
			CExpected next;
			next.fixture = label;
			next.method = method;
			next.path = path;
			next.response.networkError = false;
			next.response.status = status;
			next.response.body = "{\"success\":true,\"data\":" + data +
								 ",\"meta\":{\"timestamp\":\"2026-10-24T21:40:00.000Z\",\"requestId\":\"req_example\"}}";
			expected.push_back(next);
		}

		void Run(long long ms)
		{
			long long end = now + ms;
			for (;;)
			{
				size_t best = m_tasks.size();
				for (size_t i = 0; i < m_tasks.size(); ++i)
					if (m_tasks[i].due <= end && (best == m_tasks.size() || m_tasks[i].due < m_tasks[best].due ||
												  (m_tasks[i].due == m_tasks[best].due && m_tasks[i].seq < m_tasks[best].seq)))
						best = i;
				if (best == m_tasks.size())
					break;
				CTask task = m_tasks[best];
				m_tasks.erase(m_tasks.begin() + (long)best);
				if (task.due > now)
					now = task.due;
				task.fn();
			}
			now = end;
		}

		std::string State() const
		{
			Json value;
			if (states.empty() || !Json::Parse(states.back(), value))
				return std::string();
			return value.Get("state").AsString();
		}
		Json LastState() const
		{
			Json value;
			if (!states.empty())
				Json::Parse(states.back(), value);
			return value;
		}
		std::string LastAi() const { return aiSessions.empty() ? std::string() : aiSessions.back().first; }

		void Done(const std::string& where)
		{
			for (size_t i = 0; i < expected.size(); ++i)
				Fail(where, "expected request never sent: " + expected[i].method + " " + expected[i].path + " (" + expected[i].fixture + ")");
			expected.clear();
			if (lock.held)
				Fail(where, "the lock is still held");
		}

		// No published payload, log line or trace line carries a credential (SPEC 5, 13).
		void CheckNoSecrets(const std::string& where, const std::vector<std::string>& secrets)
		{
			std::vector<std::string> all = secrets;
			all.push_back("dsr_");
			std::vector<std::string> texts;
			texts.insert(texts.end(), states.begin(), states.end());
			texts.insert(texts.end(), providers.begin(), providers.end());
			texts.insert(texts.end(), logs.begin(), logs.end());
			texts.insert(texts.end(), trace.lines.begin(), trace.lines.end());
			texts.insert(texts.end(), browser.begin(), browser.end());
			for (size_t i = 0; i < texts.size(); ++i)
				for (size_t k = 0; k < all.size(); ++k)
					if (!all[k].empty() && texts[i].find(all[k]) != std::string::npos)
						Fail(where, "a credential reached a payload or log: " + texts[i]);
			for (size_t i = 0; i < texts.size(); ++i)
				if (texts[i].find("eyJ") != std::string::npos)
					Fail(where, "a token reached a payload or log: " + texts[i]);
			// The AI session carries the office-ai token and nothing else.
			for (size_t i = 0; i < aiSessions.size(); ++i)
			{
				if (aiSessions[i].first.find("dsr_") != std::string::npos ||
					(!officeAccessToken.empty() && aiSessions[i].first.find(officeAccessToken) != std::string::npos))
					Fail(where, "the AI session carries an Office credential");
			}
			for (size_t i = 0; i < sent.size(); ++i)
			{
				if (sent[i].url.find('?') != std::string::npos || sent[i].url.find('#') != std::string::npos)
					Fail(where, "a request URL has a query: " + sent[i].url);
				for (size_t h = 0; h < sent[i].headers.size(); ++h)
					if (Lower(sent[i].headers[h].first) == "cookie")
						Fail(where, "Office sent a cookie to the API");
			}
		}

		void Print(const std::string& title)
		{
			std::printf("\n=== %s\n", title.c_str());
			for (size_t i = 0; i < trace.lines.size(); ++i)
				std::printf("%s\n", trace.lines[i].c_str());
			trace.lines.clear();
		}

		// ---- IEnvironment ---------------------------------------------------

		long long NowMs() override { return now; }

		void Post(int delayMs, const std::function<void()>& task) override { Queue(now + delayMs, task); }

		void RunBlocking(const std::function<void()>& work, const std::function<void()>& done) override
		{
			Queue(now, [this, work, done]() {
				work();
				Queue(now, done);
			});
		}

		void Send(const CHttpRequest& request, int, const std::function<void(const CHttpResponse&)>& done) override
		{
			sent.push_back(request);
			std::string path = request.url.compare(0, std::string(kApiOrigin).size(), kApiOrigin) == 0
								   ? request.url.substr(std::string(kApiOrigin).size())
								   : request.url;
			if (expected.empty() || expected.front().method != request.method || expected.front().path != path)
			{
				Fail("http", "unexpected request " + request.method + " " + path +
								 (expected.empty() ? std::string(", none expected") : ", expected " + expected.front().method + " " + expected.front().path));
				trace.Add("http", "-> " + request.method + " " + path + "  UNEXPECTED");
				Queue(now + 40, [done]() { done(CHttpResponse()); });
				return;
			}
			CExpected next = expected.front();
			expected.pop_front();
			trace.Add("http", "-> " + request.method + " " + path + HeaderSummary(request));
			if (!next.request.IsNull())
				CheckRequest(next, request, path);
			CHttpResponse response = next.response;
			Queue(now + 40, [this, response, next, done, path]() {
				if (response.networkError)
					trace.Add("http", "<- no answer (offline)");
				else
				{
					trace.Add("http", "<- " + std::to_string(response.status) + " " + Outcome(response.body) + "  [" + next.fixture + "]");
					Json body;
					if (path.compare(0, 9, "/v1/auth/") == 0 && Json::Parse(response.body, body) && body.Get("data").Get("accessToken").IsString())
						officeAccessToken = body.Get("data").Get("accessToken").AsString();
				}
				done(response);
			});
		}

		void PublishState(const std::string& stateJson) override
		{
			Json value;
			Json::Parse(stateJson, value);
			std::string line = "state " + value.Get("state").AsString();
			if (value.Get("error").IsString())
				line += " (" + value.Get("error").AsString() + ")";
			if (states.empty() || LastStateLine() != line)
				trace.Add("page", line);
			m_lastStateLine = line;
			states.push_back(stateJson);
		}
		void AnnounceProvider(const std::string& portalLoginJson) override
		{
			Json value;
			Json::Parse(portalLoginJson, value);
			trace.Add("page", "portal:login " + value.Get("provider").AsString() + " " + value.Get("domain").AsString());
			providers.push_back(portalLoginJson);
		}
		void ClearProviderSession(const std::string& appOrigin) override
		{
			trace.Add("cef", "delete " + appOrigin + " cookies; portal:logout");
			cleared.push_back(appOrigin);
		}
		void PublishAiSession(const std::string& sessionJson, long long expiresAtMs) override
		{
			Json value;
			Json::Parse(sessionJson, value);
			trace.Add("ai", "getMarketCanopyAiSession() = " + value.Get("state").AsString() +
								(expiresAtMs ? " until " + FormatInstant(expiresAtMs) : std::string()));
			aiSessions.push_back(std::make_pair(sessionJson, expiresAtMs));
		}
		void OpenBrowser(const std::string& url) override
		{
			trace.Add("browser", "opens " + url.substr(0, url.find('?')) + " (PKCE challenge and state in the query)");
			browser.push_back(url);
		}
		void LoadConnectPost(int browserId, const std::string& url, const std::string& formBody) override
		{
			trace.Add("cef", "browser " + std::to_string(browserId) + " POSTs " + url + " (one-time code in the form body)");
			CConnectPost post = {browserId, url, formBody};
			connectPosts.push_back(post);
		}
		void Log(const std::string& line) override
		{
			trace.Add("log", line);
			logs.push_back(line);
		}

	private:
		struct CTask
		{
			long long due;
			long long seq;
			std::function<void()> fn;
		};
		std::vector<CTask> m_tasks;
		long long m_seq;
		std::string m_lastStateLine;

		const std::string& LastStateLine() const { return m_lastStateLine; }

		void Queue(long long due, const std::function<void()>& fn)
		{
			CTask task = {due, m_seq++, fn};
			m_tasks.push_back(task);
		}

		static std::string Lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return value;
		}

		static std::string HeaderSummary(const CHttpRequest& request)
		{
			std::string names;
			for (size_t i = 0; i < request.headers.size(); ++i)
			{
				const std::string& name = request.headers[i].first;
				if (name == "Authorization")
					names += " +bearer";
				else if (name == "x-active-organization-id")
					names += " +org " + request.headers[i].second.substr(20);
			}
			return names;
		}

		static std::string Outcome(const std::string& body)
		{
			Json value;
			if (!Json::Parse(body, value))
				return "(not JSON)";
			if (value.Get("success").IsBool() && !value.Get("success").AsBool())
				return value.Get("error").Get("code").AsString();
			return "ok";
		}

		void CheckRequest(const CExpected& next, const CHttpRequest& request, const std::string& path)
		{
			std::string where = "request " + next.fixture;
			const Json& headers = next.request.Get("headers");
			std::map<std::string, std::string> sentHeaders;
			for (size_t i = 0; i < request.headers.size(); ++i)
			{
				std::string name = Lower(request.headers[i].first);
				if (sentHeaders.count(name))
					Fail(where, "header sent twice: " + request.headers[i].first);
				sentHeaders[name] = request.headers[i].second;
			}
			std::set<std::string> listed;
			for (size_t i = 0; i < headers.Members().size(); ++i)
			{
				std::string name = Lower(headers.Members()[i].first);
				const std::string& want = headers.Members()[i].second.AsString();
				listed.insert(name);
				std::map<std::string, std::string>::const_iterator got = sentHeaders.find(name);
				if (got == sentHeaders.end())
				{
					if (!next.rule.mayOmit.count(headers.Members()[i].first))
						Fail(where, "header missing: " + headers.Members()[i].first);
					continue;
				}
				if (name == "authorization")
				{
					if (officeAccessToken.empty() || got->second != "Bearer " + officeAccessToken)
						Fail(where, "Authorization is not the Office access token issued last");
				}
				else if (name == "x-client-platform")
					CHECK_EQ(got->second, std::string(OsPlatformName()), where);
				else if (got->second != want && !next.rule.mayDiffer.count(headers.Members()[i].first))
					Fail(where, headers.Members()[i].first + " is " + got->second + ", the fixture has " + want);
			}
			for (std::map<std::string, std::string>::const_iterator it = sentHeaders.begin(); it != sentHeaders.end(); ++it)
				if (!listed.count(it->first) && it->first != "content-type")
					Fail(where, "header the fixture does not send: " + it->first);
			if (request.method == "GET")
			{
				CHECK(request.body.empty(), where);
				return;
			}
			Json body;
			if (!Json::Parse(request.body, body))
			{
				Fail(where, "body is not JSON");
				return;
			}
			if (next.rule.bodyExact && next.request.Has("body"))
				CheckSame(body, next.request.Get("body"), where + " body");
			if (next.rule.body)
				next.rule.body(body);
			(void)path;
		}
	};
}

#endif // MARKETCANOPY_FAKE_ENVIRONMENT_H
