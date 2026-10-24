/*
 * Market Canopy Office: the account module in the browser process (CEF and the shells).
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

// IEnvironment on CEF's UI thread. The account module (account.h) runs on
// that thread only; credential store and lock calls run on threads of ours,
// so a keychain prompt or a 30-second lock wait never blocks CEF.
// HTTP goes through CefURLRequest without stored credentials: no cookie of
// the connect page or a cloud ever rides along with an API call.
//
// Start pages talk to it with execCommand("mc:account", {"action": ...}) and
// hear back through on_native_message("mc:account", <state JSON>), the same
// channel upstream's start page already uses for portal:login.

#include "additional.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "include/cef_request.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"

#include "../../include/applicationmanager.h"
#include "../../include/cefview.h"
#include "account.h"
#include "contract.h"
#include "hooks.h"
#include "platform.h"

#ifndef MARKETCANOPY_OFFICE_VERSION
#define MARKETCANOPY_OFFICE_VERSION "0.0.0-mc.0"
#endif
#ifndef MARKETCANOPY_OFFICE_FLAVOR
#define MARKETCANOPY_OFFICE_FLAVOR "staging"
#endif

#ifdef MESSAGE_IN_BROWSER
#define MC_SEND_TO_RENDERER(frame, message) (frame)->GetBrowser()->SendProcessMessage(PID_RENDERER, message)
#else
#define MC_SEND_TO_RENDERER(frame, message) (frame)->SendProcessMessage(PID_RENDERER, message)
#endif

namespace NSMarketCanopy
{
	namespace
	{
		const int kMaxResponseBytes = 1024 * 1024;

		std::string Utf8(const std::wstring& value)
		{
			return CefString(value).ToString();
		}

		std::wstring Wide(const std::string& value)
		{
			return CefString(value).ToWString();
		}

		class CFunctionTask : public CefTask
		{
		public:
			explicit CFunctionTask(const std::function<void()>& task) : m_task(task) {}
			void Execute() override { m_task(); }

		private:
			std::function<void()> m_task;
			IMPLEMENT_REFCOUNTING(CFunctionTask);
		};

		// Set while the glue exists; tasks posted earlier do nothing once it is gone.
		std::shared_ptr<bool> g_alive;

		void PostToUi(int delayMs, const std::function<void()>& task)
		{
			std::weak_ptr<bool> alive = g_alive;
			CefRefPtr<CefTask> wrapped = new CFunctionTask([alive, task]() {
				if (!alive.expired())
					task();
			});
			if (delayMs > 0)
				CefPostDelayedTask(TID_UI, wrapped, delayMs);
			else
				CefPostTask(TID_UI, wrapped);
		}

		// Each store or lock call gets a thread of its own: a rotation holds the
		// lock across two calls, so a queue shared with another call that waits
		// for the lock would stall until the lock times out.
		void RunDetached(const std::function<void()>& work)
		{
			std::thread(work).detach();
		}

		class CRequestClient : public CefURLRequestClient
		{
		public:
			explicit CRequestClient(const std::function<void(const CHttpResponse&)>& done) : m_done(done), m_finished(false) {}

			void OnRequestComplete(CefRefPtr<CefURLRequest> request) override
			{
				if (m_finished)
					return;
				m_finished = true;
				CHttpResponse response;
				CefRefPtr<CefResponse> answer = request->GetResponse();
				if (request->GetRequestStatus() == UR_SUCCESS && answer && answer->GetStatus() > 0)
				{
					response.networkError = false;
					response.status = answer->GetStatus();
					response.body = m_body;
				}
				std::function<void(const CHttpResponse&)> done = m_done;
				m_done = nullptr;
				done(response);
			}

			void OnUploadProgress(CefRefPtr<CefURLRequest>, int64, int64) override {}
			void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64, int64) override {}

			void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data, size_t length) override
			{
				if (m_body.size() + length > (size_t)kMaxResponseBytes)
				{
					request->Cancel();
					return;
				}
				m_body.append((const char*)data, length);
			}

			bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
									CefRefPtr<CefAuthCallback>) override
			{
				return false;
			}

		private:
			std::function<void(const CHttpResponse&)> m_done;
			std::string m_body;
			bool m_finished;
			IMPLEMENT_REFCOUNTING(CRequestClient);
		};

		bool IsStartPage(CCefView* view)
		{
			if (!view || view->GetType() != cvwtSimple)
				return false;
			std::wstring url = view->GetUrl();
			return url.compare(0, 8, L"file:///") == 0;
		}

		class CBrowserGlue : public IEnvironment
		{
		public:
			explicit CBrowserGlue(CAscApplicationManager* manager)
				: m_manager(manager), m_store(NULL), m_lock(NULL), m_started(false), m_startPageViewId(-1),
				  m_portalLogoutFromPage(false), m_aiExpiresMs(0)
			{
			}

			~CBrowserGlue() override
			{
				m_account.reset();
				// A store or lock call may still be running on its thread; it keeps
				// using these, and the process is closing, so they are left to it.
				m_store = NULL;
				m_lock = NULL;
			}

			CAccount* Account()
			{
				if (!m_account)
				{
					CAccount::CConfig config;
					config.flavor = std::string(MARKETCANOPY_OFFICE_FLAVOR) == "production" ? EFlavor::Production : EFlavor::Staging;
					config.version = MARKETCANOPY_OFFICE_VERSION;
					std::string service = ServiceName(config.flavor);
					m_store = CreateOsCredentialStore(service);
					m_lock = CreateOsStoreLock(service);
					m_account.reset(new CAccount(this, m_store, m_lock, config));
				}
				return m_account.get();
			}

			void EnsureStarted()
			{
				if (m_started)
					return;
				m_started = true;
				Account()->Start();
			}

			// ---- IEnvironment ----

			long long NowMs() override
			{
				return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
						   std::chrono::system_clock::now().time_since_epoch())
					.count();
			}

			void Post(int delayMs, const std::function<void()>& task) override
			{
				PostToUi(delayMs, task);
			}

			void RunBlocking(const std::function<void()>& work, const std::function<void()>& done) override
			{
				RunDetached([work, done]() {
					work();
					PostToUi(0, done);
				});
			}

			void Send(const CHttpRequest& request, int timeoutMs, const std::function<void(const CHttpResponse&)>& done) override
			{
				CefRefPtr<CefRequest> cefRequest = CefRequest::Create();
				cefRequest->SetURL(request.url);
				cefRequest->SetMethod(request.method);
				CefRequest::HeaderMap headers;
				for (size_t i = 0; i < request.headers.size(); ++i)
					headers.insert(std::make_pair(CefString(request.headers[i].first), CefString(request.headers[i].second)));
				cefRequest->SetHeaderMap(headers);
				if (request.method != "GET")
				{
					CefRefPtr<CefPostData> body = CefPostData::Create();
					CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
					element->SetToBytes(request.body.size(), request.body.data());
					body->AddElement(element);
					cefRequest->SetPostData(body);
				}
				// No cookies, no HTTP auth, no cache, no redirects (a redirect is an error here).
				cefRequest->SetFlags(UR_FLAG_DISABLE_CACHE | UR_FLAG_STOP_ON_REDIRECT | UR_FLAG_NO_RETRY_ON_5XX);
				CefRefPtr<CRequestClient> client = new CRequestClient([done](const CHttpResponse& response) { done(response); });
				CefRefPtr<CefURLRequest> urlRequest = CefURLRequest::Create(cefRequest, client.get(), nullptr);
				if (!urlRequest)
				{
					CHttpResponse failed;
					PostToUi(0, [done, failed]() { done(failed); });
					return;
				}
				PostToUi(timeoutMs, [urlRequest]() {
					if (urlRequest->GetRequestStatus() == UR_IO_PENDING || urlRequest->GetRequestStatus() == UR_UNKNOWN)
						urlRequest->Cancel();
				});
			}

			void PublishState(const std::string& stateJson) override
			{
				SendToStartPages(L"mc:account", Wide(stateJson));
			}

			void AnnounceProvider(const std::string& portalLoginJson) override
			{
				// What a cloud's own page sends after its sign-in (SPEC 7.2); the start
				// page lists the connection and checks it.
				SendToStartPages(L"portal:login", Wide(portalLoginJson));
			}

			void ClearProviderSession(const std::string& appOrigin) override
			{
				std::wstring origin = Wide(appOrigin);
				m_manager->Logout(origin);
				SendToStartPages(L"portal:logout", origin);
				if (m_portalLogoutFromPage)
				{
					// the start page asked the shell already
					m_portalLogoutFromPage = false;
					return;
				}
				// As the start page's own "Log out" does: the shell closes that cloud's
				// editors and forgets the connection.
				NSEditorApi::CAscCefMenuEventListener* listener = m_manager->GetEventListener();
				if (!listener)
					return;
				NSEditorApi::CAscCefMenuEvent* event = new NSEditorApi::CAscCefMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND);
				event->put_SenderId(m_startPageViewId);
				NSEditorApi::CAscExecCommand* data = new NSEditorApi::CAscExecCommand();
				data->put_Command(L"portal:logout");
				Json param = Json::MakeObject();
				param.Set("domain", Json::MakeString(appOrigin));
				data->put_Param(Wide(param.Dump()));
				event->m_pData = data;
				listener->OnEvent(event);
			}

			void PublishAiSession(const std::string& sessionJson, long long expiresAtMs) override
			{
				m_aiSession = sessionJson;
				m_aiExpiresMs = expiresAtMs;
				std::vector<CefRefPtr<CefFrame> > frames;
				for (size_t i = 0; i < m_aiFrames.size(); ++i)
				{
					if (SendAiSession(m_aiFrames[i]))
						frames.push_back(m_aiFrames[i]);
				}
				m_aiFrames.swap(frames);
			}

			void OpenBrowser(const std::string& url) override
			{
				NSEditorApi::CAscCefMenuEventListener* listener = m_manager->GetEventListener();
				if (!listener)
					return;
				NSEditorApi::CAscCefMenuEvent* event = new NSEditorApi::CAscCefMenuEvent(ASC_MENU_EVENT_TYPE_CEF_ONOPENLINK);
				NSEditorApi::CAscOnOpenExternalLink* data = new NSEditorApi::CAscOnOpenExternalLink();
				data->put_Url(Wide(url));
				event->m_pData = data;
				listener->OnEvent(event);
			}

			void LoadConnectPost(int browserId, const std::string& url, const std::string& formBody) override
			{
				std::map<int, CefRefPtr<CefBrowser> >::iterator found = m_connectBrowsers.find(browserId);
				if (found == m_connectBrowsers.end() || !found->second->IsValid())
					return;
				CefRefPtr<CefFrame> frame = found->second->GetMainFrame();
				// the frame must still be on the connect page: LoadRequest is same-origin only
				if (!frame || frame->GetURL().ToString().compare(0, url.size(), url) != 0)
					return;
				CefRefPtr<CefRequest> request = CefRequest::Create();
				request->SetURL(url);
				request->SetMethod("POST");
				CefRequest::HeaderMap headers;
				headers.insert(std::make_pair(CefString("Content-Type"), CefString("application/x-www-form-urlencoded")));
				request->SetHeaderMap(headers);
				CefRefPtr<CefPostData> body = CefPostData::Create();
				CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
				element->SetToBytes(formBody.size(), formBody.data());
				body->AddElement(element);
				request->SetPostData(body);
				m_postPending[browserId] = true;
				frame->LoadRequest(request);
			}

			void Log(const std::string& line) override
			{
				// Lines never carry a token, a code or a URL (platform.h). Off unless asked for.
				static const bool enabled = getenv("MARKETCANOPY_OFFICE_ACCOUNT_LOG") != NULL;
				if (enabled)
					fprintf(stderr, "[marketcanopy account] %s\n", line.c_str());
			}

			// ---- hooks ----

			bool ExecCommand(CCefView* view, const std::wstring& command, const std::wstring& param)
			{
				if (command == L"portal:logout")
				{
					// The start page's own "Log out" of the Market Canopy connection also
					// signs Office out; the shell goes on with the logout it asked for.
					if (!IsStartPage(view) || !m_account)
						return false;
					Json value;
					if (Json::Parse(Utf8(param), value) && value.Get("domain").IsString() &&
						TrimSlash(value.Get("domain").AsString()) == OriginsFor(Flavor()).app)
					{
						m_portalLogoutFromPage = true;
						PostToUi(60000, [this]() { m_portalLogoutFromPage = false; });
						m_account->SignOut();
					}
					return false;
				}
				if (command.compare(0, 3, L"mc:") != 0)
					return false;
				// Only a start page drives the account; anything else asking is ignored.
				if (!IsStartPage(view))
					return true;
				m_startPageViewId = view->GetId();
				EnsureStarted();
				Json value;
				std::string action;
				if (Json::Parse(Utf8(param), value) && value.Get("action").IsString())
					action = value.Get("action").AsString();
				CAccount* account = Account();
				if (action == "state")
					PublishState(account->StateJson());
				else if (action == "activate")
					account->OnActivate();
				else if (action == "signIn")
					account->SignIn();
				else if (action == "cancelSignIn")
					account->CancelSignIn();
				else if (action == "signOut")
					account->SignOut();
				else if (action == "selectOrganization" && value.Get("organizationId").IsString())
					account->SelectOrganization(value.Get("organizationId").AsString());
				else if (action == "openStore")
					OpenStore(account);
				return true;
			}

			void MainFrameLoaded(CCefView* view, CefRefPtr<CefBrowser> browser)
			{
				if (IsStartPage(view))
				{
					m_startPageViewId = view->GetId();
					if (m_started)
						PublishState(Account()->StateJson());
					EnsureStarted();
					return;
				}
				if (!m_account || !browser || !browser->GetMainFrame())
					return;
				int id = browser->GetIdentifier();
				std::map<int, bool>::iterator pending = m_postPending.find(id);
				if (pending != m_postPending.end())
				{
					// the answer to our own POST, not a new visit
					m_postPending.erase(pending);
					return;
				}
				m_connectBrowsers[id] = browser;
				if (!m_account->OnPageLoaded(id, browser->GetMainFrame()->GetURL().ToString()))
					m_connectBrowsers.erase(id);
			}

			int SchemeCommand(const std::wstring& argument)
			{
				std::string url = Utf8(argument);
				if (!IsSchemeUrl(url))
					return 0;
				if (ParseSchemeUrl(url).kind == CSchemeLink::Open)
					return 0;
				// A sign-in callback, or a link 4.1 refuses: never opened. The shells may
				// call from their own thread, so the module sees it on CEF's.
				PostToUi(0, [this, url]() { Account()->HandleSchemeUrl(url); });
				return 1;
			}

			bool AiSessionRequest(CefRefPtr<CefBrowser> browser, CefRefPtr<CefProcessMessage> message)
			{
				CefRefPtr<CefListValue> args = message->GetArgumentList();
				if (!browser || args->GetSize() < 1 || args->GetType(0) != VTYPE_STRING)
					return true;
				char* end = NULL;
				std::string identifier = args->GetString(0).ToString();
				long long frameId = strtoll(identifier.c_str(), &end, 10);
				if (identifier.empty() || !end || *end != '\0')
					return true;
				CefRefPtr<CefFrame> frame = browser->GetFrame(frameId);
				// The browser process checks the frame again: a renderer's word is not enough.
				if (!frame || !IsAiSessionFrame(frame->GetURL().ToWString(), m_manager->m_oSettings.system_plugins_path))
					return true;
				bool known = false;
				for (size_t i = 0; i < m_aiFrames.size(); ++i)
				{
					if (m_aiFrames[i]->GetIdentifier() == frame->GetIdentifier() &&
						m_aiFrames[i]->GetBrowser()->GetIdentifier() == browser->GetIdentifier())
						known = true;
				}
				if (!known)
					m_aiFrames.push_back(frame);
				EnsureStarted();
				if (!m_aiSession.empty())
					SendAiSession(frame);
				Account()->RequestAiSession();
				return true;
			}

		private:
			CAscApplicationManager* m_manager;
			ICredentialStore* m_store;
			IStoreLock* m_lock;
			std::unique_ptr<CAccount> m_account;
			bool m_started;
			int m_startPageViewId;
			bool m_portalLogoutFromPage;
			std::map<int, CefRefPtr<CefBrowser> > m_connectBrowsers;
			std::map<int, bool> m_postPending;
			std::vector<CefRefPtr<CefFrame> > m_aiFrames;
			std::string m_aiSession;
			long long m_aiExpiresMs;

			static EFlavor Flavor()
			{
				return std::string(MARKETCANOPY_OFFICE_FLAVOR) == "production" ? EFlavor::Production : EFlavor::Staging;
			}

			static std::string TrimSlash(const std::string& value)
			{
				return !value.empty() && value[value.size() - 1] == '/' ? value.substr(0, value.size() - 1) : value;
			}

			void OpenStore(CAccount* account)
			{
				Json state;
				if (!Json::Parse(account->StateJson(), state))
					return;
				const Json& storeUrl = state.Get("update").Get("storeUrl");
				if (account->GetState() == CAccount::UpdateRequired && storeUrl.IsString() &&
					storeUrl.AsString().compare(0, 8, "https://") == 0)
					OpenBrowser(storeUrl.AsString());
			}

			void SendToStartPages(const std::wstring& command, const std::wstring& param)
			{
				std::vector<int> ids = m_manager->GetViewsId();
				for (size_t i = 0; i < ids.size(); ++i)
				{
					CCefView* view = m_manager->GetViewById(ids[i]);
					if (!IsStartPage(view))
						continue;
					NSEditorApi::CAscMenuEvent* event = new NSEditorApi::CAscMenuEvent(ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND_JS);
					NSEditorApi::CAscExecCommandJS* data = new NSEditorApi::CAscExecCommandJS();
					data->put_Command(command);
					data->put_Param(param);
					event->m_pData = data;
					view->Apply(event);
				}
			}

			// false: the frame is gone or no longer an AI plugin, and is dropped
			bool SendAiSession(CefRefPtr<CefFrame> frame)
			{
				if (!frame || !frame->IsValid() ||
					!IsAiSessionFrame(frame->GetURL().ToWString(), m_manager->m_oSettings.system_plugins_path))
					return false;
				CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("mc_ai_session");
				message->GetArgumentList()->SetString(0, m_aiSession);
				message->GetArgumentList()->SetDouble(1, (double)m_aiExpiresMs);
				message->GetArgumentList()->SetString(2, CefString(std::to_string(frame->GetIdentifier())));
				MC_SEND_TO_RENDERER(frame, message);
				return true;
			}
		};

		CBrowserGlue* g_glue = NULL;
	}

	bool OnExecCommand(CCefView* view, CefRefPtr<CefBrowser>, const std::wstring& command, const std::wstring& param)
	{
		return g_glue ? g_glue->ExecCommand(view, command, param) : false;
	}

	void OnMainFrameLoaded(CCefView* view, CefRefPtr<CefBrowser> browser, int)
	{
		if (g_glue)
			g_glue->MainFrameLoaded(view, browser);
	}

	int CallCommand(CAscApplicationManager*, const std::wstring& command)
	{
		const std::wstring prefix = L"mc:scheme:";
		if (!g_glue || command.compare(0, prefix.size(), prefix) != 0)
			return 0;
		return g_glue->SchemeCommand(command.substr(prefix.size()));
	}
}

CMarketCanopyManagerAdditional::CMarketCanopyManagerAdditional(CAscApplicationManager* pManager)
	: CApplicationManagerAdditionalBase(pManager)
{
	NSMarketCanopy::g_alive.reset(new bool(true));
	NSMarketCanopy::g_glue = new NSMarketCanopy::CBrowserGlue(pManager);
}

CMarketCanopyManagerAdditional::~CMarketCanopyManagerAdditional()
{
	NSMarketCanopy::g_alive.reset();
	delete NSMarketCanopy::g_glue;
	NSMarketCanopy::g_glue = NULL;
}

bool CMarketCanopyManagerAdditional::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefProcessId,
															  CefRefPtr<CefProcessMessage> message, CCefView*)
{
	if (!NSMarketCanopy::g_glue || message->GetName() != "mc_ai_session_request")
		return false;
	return NSMarketCanopy::g_glue->AiSessionRequest(browser, message);
}
