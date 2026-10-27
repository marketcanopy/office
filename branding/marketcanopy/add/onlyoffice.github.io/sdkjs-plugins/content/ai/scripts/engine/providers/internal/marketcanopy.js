/*
 * Market Canopy Office: the managed "Market Canopy" provider of the AI plugin.
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

// ADR 0525: Market Canopy is the plugin's managed provider, listed first and
// selected by default. Since the amendment of 2026-10-26 the person may also
// use upstream's bundled providers with their own keys. The engine evaluates
// this file as `(function(){ <file> return new Provider(); })()` while it loads
// the providers config.json lists, before it calls AI.onLoadInternalProviders.
//
// - The base URL, the model aliases and a short-lived token come from
//   window.AscDesktopEditor.getMarketCanopyAiSession() (Office contract
//   section 9) on every request. The token is never stored: the provider's
//   key is always empty, and its URL is the session's.
// - Once the providers load, Market Canopy's models are the session's
//   aliases, and an action with no usable model is pointed at one. The
//   person's own providers, models and choices are kept.
// - Custom (JavaScript file) and external providers cannot be added, and
//   stored ones are removed: a custom provider's code would run in this page,
//   which can read the Market Canopy session.
// - When the account or organization changes, requests in flight to the
//   gateway are cancelled and the models reload.

"use strict";

var MC = window.MarketCanopyAI;

if (!MC) {
	MC = window.MarketCanopyAI = (function() {
		var NAME = "Market Canopy";
		var BASE_URL = /^https:\/\/[a-z0-9.-]+\/v1\/office\/ai$/;
		var ALIAS = /^[a-z0-9][a-z0-9.-]{0,63}$/;
		var CAPABILITIES = ["chat", "tools", "vision", "reasoning"];
		var STATES = ["signed_out", "offline", "update_required", "not_entitled"];
		var LAST_MODELS_KEY = "marketcanopy-plugin-models";
		var FALLBACK_MODELS = [{ id: "mc-standard", name: "Canopy", capabilities: ["chat"] }];
		var WATCH_INTERVAL_MS = 3000;
		// register.js's action types and what each needs.
		var ACTIONS_KEY = "onlyoffice_ai_actions_key";
		var ACTION_CAPABILITIES = {
			Chat: "chat",
			Summarization: "chat",
			Translation: "chat",
			TextAnalyze: "chat",
			ImageGeneration: "image",
			OCR: "vision",
			Vision: "vision"
		};
		var MESSAGES = {
			signed_out: "Sign in to Market Canopy on the Home screen to use Market Canopy AI.",
			offline: "AI needs a network connection.",
			update_required: "Update Market Canopy Office to use AI.",
			not_entitled: "Your Market Canopy plan does not include AI.",
			account_changed: "Your Market Canopy account or organization changed. Try again."
		};

		function isString(value) {
			return typeof value === "string" && value.length > 0;
		}

		function parseModel(value) {
			if (!value || typeof value !== "object")
				return null;
			if (!isString(value.id) || !ALIAS.test(value.id) || !isString(value.name))
				return null;
			var capabilities = Array.isArray(value.capabilities) ?
				value.capabilities.filter(function(item) { return CAPABILITIES.indexOf(item) !== -1; }) : [];
			return { id: value.id, name: value.name, capabilities: capabilities };
		}

		// Anything that is not a well-formed ready session or a known state
		// reads as offline, so a broken answer never reaches a request.
		function parseSession(raw) {
			var value = raw;
			if (typeof raw === "string") {
				try {
					value = JSON.parse(raw);
				} catch (e) {
					return { state: "offline" };
				}
			}
			if (!value || typeof value !== "object")
				return { state: "offline" };
			if (value.state !== "ready")
				return { state: STATES.indexOf(value.state) !== -1 ? value.state : "offline" };

			var models = Array.isArray(value.models) ?
				value.models.map(parseModel).filter(function(model) { return model !== null; }) : [];
			if (!isString(value.baseUrl) || !BASE_URL.test(value.baseUrl) ||
				!isString(value.accessToken) || !isString(value.expiresAt) ||
				!isString(value.organizationId) || !isString(value.defaultModel) ||
				models.length === 0)
				return { state: "offline" };

			var known = models.some(function(model) { return model.id === value.defaultModel; });
			return {
				state: "ready",
				baseUrl: value.baseUrl,
				accessToken: value.accessToken,
				expiresAt: value.expiresAt,
				organizationId: value.organizationId,
				models: models,
				defaultModel: known ? value.defaultModel : models[0].id
			};
		}

		// Outside the two bundled plugin pages native code offers no method:
		// the person is treated as signed out.
		function readSession() {
			var host = window.AscDesktopEditor;
			if (!host || typeof host.getMarketCanopyAiSession !== "function")
				return { state: "signed_out" };
			try {
				return parseSession(host.getMarketCanopyAiSession());
			} catch (e) {
				return { state: "offline" };
			}
		}

		// The account, organization and aliases, without the token.
		function sessionKey(session) {
			if (session.state !== "ready")
				return session.state;
			return [session.state, session.baseUrl, session.organizationId,
				session.models.map(function(model) { return model.id; }).join(",")].join("|");
		}

		// The session's aliases, else the last ones seen (no secret).
		function knownModels(session) {
			if (session.state === "ready") {
				try {
					window.localStorage.setItem(LAST_MODELS_KEY, JSON.stringify(session.models));
				} catch (e) {
				}
				return session.models;
			}
			try {
				var last = JSON.parse(window.localStorage.getItem(LAST_MODELS_KEY));
				if (Array.isArray(last)) {
					last = last.map(parseModel).filter(function(model) { return model !== null; });
					if (last.length)
						return last;
				}
			} catch (e) {
			}
			return FALLBACK_MODELS;
		}

		function hasAction(actions, key) {
			return actions && Object.prototype.hasOwnProperty.call(actions, key);
		}

		// Removes custom and external providers, replaces Market Canopy's
		// models with the session's aliases, keeps the person's own providers
		// and models, and points each action without a usable model at a
		// Market Canopy alias. Idempotent.
		function configure() {
			var AI = window.AI;
			var session = readSession();
			var list = knownModels(session);

			var dropped = Object.keys(AI.InternalCustomProvidersSources || {});
			AI.InternalCustomProviders.concat(AI.ExternalCustomProviders).forEach(function(item) {
				dropped.push(item.name);
			});
			AI.InternalCustomProvidersSources = {};
			AI.InternalCustomProviders = [];
			AI.ExternalCustomProviders = [];
			for (var d = 0; d < dropped.length; d++) {
				if (dropped[d] !== NAME)
					delete AI.Providers[dropped[d]];
			}
			var provider = AI.Providers[NAME];
			if (!provider) {
				provider = AI.createProviderInstance(NAME, "", "");
				AI.Providers[NAME] = provider;
			}

			provider.models = [];
			provider.modelsUI = [];
			var managed = [];
			for (var i = 0; i < list.length; i++) {
				var model = {
					id: list[i].id,
					name: list[i].name,
					capabilities: list[i].capabilities,
					endpoints: [],
					options: {}
				};
				var modelUI = new AI.UI.Model(model.name, model.id, NAME, provider.checkModelCapability(model));
				provider.models.push(model);
				provider.modelsUI.push(modelUI);
				managed.push(modelUI);
			}
			var own = (AI.Models || []).filter(function(item) {
				return item && item.provider !== NAME && AI.Providers[item.provider];
			});
			var models = managed.concat(own);
			AI.Models = models;

			var ids = models.map(function(item) { return item.id; });
			var managedIds = managed.map(function(item) { return item.id; });
			var chat = session.state === "ready" && managedIds.indexOf(session.defaultModel) !== -1 ?
				session.defaultModel : managedIds[0];
			var visionModel = managed.filter(function(item) {
				return (item.capabilities & AI.CapabilitiesUI.Vision) !== 0;
			})[0];
			var choose = function(type, current) {
				var wanted = ACTION_CAPABILITIES[type] === "vision" ? AI.CapabilitiesUI.Vision :
					ACTION_CAPABILITIES[type] === "image" ? AI.CapabilitiesUI.Image : AI.CapabilitiesUI.Chat;
				var model = models[ids.indexOf(current)];
				if (model && (model.capabilities & wanted) !== 0)
					return current;
				if (wanted === AI.CapabilitiesUI.Vision)
					return visionModel ? visionModel.id : "";
				if (wanted === AI.CapabilitiesUI.Chat)
					return chat;
				return ""; // image generation: the gateway has none
			};
			if (AI.Actions) {
				for (var type in ACTION_CAPABILITIES) {
					if (AI.Actions[type])
						AI.Actions[type].model = choose(type, AI.Actions[type].model);
				}
				AI.ActionsSave();
			} else {
				// The actions register after the providers load and read their
				// models from storage, so storage holds the choice.
				var stored = null;
				try {
					stored = JSON.parse(window.localStorage.getItem(ACTIONS_KEY));
				} catch (e) {
				}
				if (!stored || typeof stored !== "object")
					stored = {};
				for (var item in ACTION_CAPABILITIES) {
					var entry = stored[item] && typeof stored[item] === "object" ? stored[item] : {};
					entry.model = choose(item, entry.model);
					stored[item] = entry;
				}
				try {
					window.localStorage.setItem(ACTIONS_KEY, JSON.stringify(stored));
				} catch (e) {
				}
			}
			AI.Storage.save();
		}

		// Requests in flight to the gateway, so an account or organization
		// change can cancel them. The engine calls the global fetch.
		var inFlight = [];
		var originalFetch = window.fetch;
		function isGatewayRequest(input) {
			var url = typeof input === "string" ? input : (input && input.url) || "";
			var session = readSession();
			return session.state === "ready" && url.indexOf(session.baseUrl + "/") === 0;
		}
		function trackedFetch(input, init) {
			if (!originalFetch || !isGatewayRequest(input))
				return originalFetch.apply(this, arguments);
			var controller = new AbortController();
			var options = Object.assign({}, init || {}, { credentials: "omit" });
			var upstream = options.signal;
			if (upstream) {
				if (upstream.aborted)
					controller.abort(upstream.reason);
				else
					upstream.addEventListener("abort", function() { controller.abort(upstream.reason); });
			}
			options.signal = controller.signal;
			inFlight.push(controller);
			var release = function() {
				var index = inFlight.indexOf(controller);
				if (index !== -1)
					inFlight.splice(index, 1);
			};
			return originalFetch.call(this, input, options).then(function(response) {
				if (!response.body)
					release();
				return response;
			}, function(error) {
				release();
				throw error;
			});
		}
		function cancelAll() {
			var controllers = inFlight.slice();
			inFlight.length = 0;
			// fetch rejects with the reason, which the engine shows.
			for (var i = 0; i < controllers.length; i++)
				controllers[i].abort(new Error(MESSAGES.account_changed));
			return controllers.length;
		}

		var lastKey = "";
		// Returns whether the session changed; on a change after the first
		// check, cancels requests in flight and reloads the models.
		function check() {
			var key = sessionKey(readSession());
			if (key === lastKey)
				return false;
			var first = lastKey === "";
			lastKey = key;
			if (!first) {
				cancelAll();
				configure();
			}
			return true;
		}

		var installed = false;
		function install() {
			var AI = window.AI;
			if (installed || !AI)
				return;
			installed = true;

			var onLoadInternalProviders = AI.onLoadInternalProviders;
			AI.onLoadInternalProviders = function() {
				onLoadInternalProviders.apply(this, arguments);
				configure();
				check();
			};

			AI.addCustomProvider = function() {
				return false;
			};
			AI.addExternalProvider = function() {
				return false;
			};

			// code.js would add the start window agent's selected model as an
			// external model with a provider built from the agent's stored
			// settings. The plugin has its own Market Canopy models, so that
			// model is never added.
			Object.defineProperty(AI, "DEFAULT_DESKTOP_MODEL", {
				configurable: true,
				get: function() { return null; },
				set: function() {}
			});

			if (originalFetch)
				window.fetch = trackedFetch;
			if (window.AscDesktopEditor && typeof window.AscDesktopEditor.getMarketCanopyAiSession === "function")
				window.setInterval(check, WATCH_INTERVAL_MS);
		}

		return {
			NAME: NAME,
			MESSAGES: MESSAGES,
			parseSession: parseSession,
			readSession: readSession,
			sessionKey: sessionKey,
			knownModels: knownModels,
			configure: configure,
			check: check,
			cancelAll: cancelAll,
			inFlightCount: function() { return inFlight.length; },
			install: install
		};
	})();
}

MC.install();

class Provider extends AI.Provider {

	constructor() {
		super(MC.NAME, "", "", "");
	}

	// The session's base URL; a stored or typed URL is ignored.
	get url() {
		var session = MC.readSession();
		return session.state === "ready" ? session.baseUrl : "";
	}

	set url(value) {
	}

	// Never a key: the token is read for each request.
	get key() {
		return "";
	}

	set key(value) {
	}

	isOnlyDesktop() {
		return true;
	}

	isUseProxy() {
		return false;
	}

	// The aliases; the engine makes no /models request.
	getModels() {
		return MC.knownModels(MC.readSession()).map(function(model) {
			return { id: model.id, name: model.name, capabilities: model.capabilities };
		});
	}

	correctModelInfo(model) {
		if (!model.name)
			model.name = model.id;
	}

	checkModelCapability(model) {
		model.endpoints.push(AI.Endpoints.Types.v1.Chat_Completions);
		var capabilities = AI.CapabilitiesUI.Chat;
		var declared = Array.isArray(model.capabilities) ? model.capabilities : [];
		if (declared.indexOf("vision") !== -1)
			capabilities |= AI.CapabilitiesUI.Vision;
		if (declared.indexOf("tools") !== -1)
			capabilities |= AI.CapabilitiesUI.Tools;
		return capabilities;
	}

	// Read for every request. Nothing is sent while the session is not ready:
	// the engine shows the message.
	getRequestHeaderOptions() {
		var session = MC.readSession();
		if (session.state !== "ready")
			throw { error: 1, message: MC.MESSAGES[session.state] };
		return {
			"Content-Type": "application/json",
			"Authorization": "Bearer " + session.accessToken,
			"x-active-organization-id": session.organizationId
		};
	}

}
