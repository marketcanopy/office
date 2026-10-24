/*
 * Market Canopy Office: the Secret Service credential store (SPEC 5.3, Linux).
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

// libsecret (LGPL-2.1-or-later) is loaded at run time, so the editors start
// and edit local files on a system without it; that system gets the
// memory-only session of SPEC 5.4 rule 6. The declarations below are
// libsecret 0.20's ABI (secret-schema.h, secret-password.h), which has been
// stable since 0.18.

#if !defined(_WIN32) && !defined(__APPLE__)

#include "platform.h"

#include <dlfcn.h>
#include <mutex>

namespace NSMarketCanopy
{
	namespace
	{
		struct GErrorAbi
		{
			unsigned int domain;
			int code;
			char* message;
		};

		struct SecretSchemaAttributeAbi
		{
			const char* name;
			int type; // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
		};

		struct SecretSchemaAbi
		{
			const char* name;
			int flags; // SECRET_SCHEMA_DONT_MATCH_NAME = 1 << 1
			SecretSchemaAttributeAbi attributes[32];
			int reserved;
			void* reserved1;
			void* reserved2;
			void* reserved3;
			void* reserved4;
			void* reserved5;
			void* reserved6;
			void* reserved7;
		};

		typedef char* (*TLookup)(const SecretSchemaAbi*, void*, GErrorAbi**, ...);
		typedef int (*TStore)(const SecretSchemaAbi*, const char*, const char*, const char*, void*, GErrorAbi**, ...);
		typedef int (*TClear)(const SecretSchemaAbi*, void*, GErrorAbi**, ...);
		typedef void (*TFreeString)(char*);
		typedef void (*TFreeError)(GErrorAbi*);

		struct CLibSecret
		{
			bool loaded;
			TLookup lookup;
			TStore store;
			TClear clear;
			TFreeString wipe;
			TFreeError freeError;
			CLibSecret() : loaded(false), lookup(NULL), store(NULL), clear(NULL), wipe(NULL), freeError(NULL) {}
		};

		const CLibSecret& LibSecret()
		{
			static CLibSecret library;
			static std::once_flag once;
			std::call_once(once, []() {
				void* secret = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
				void* glib = dlopen("libglib-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
				if (!secret || !glib)
					return;
				library.lookup = (TLookup)dlsym(secret, "secret_password_lookup_sync");
				library.store = (TStore)dlsym(secret, "secret_password_store_sync");
				library.clear = (TClear)dlsym(secret, "secret_password_clear_sync");
				library.wipe = (TFreeString)dlsym(secret, "secret_password_free");
				library.freeError = (TFreeError)dlsym(glib, "g_error_free");
				library.loaded = library.lookup && library.store && library.clear && library.wipe && library.freeError;
				// Never dlclose: GLib registers types that outlive any one call.
			});
			return library;
		}

		// Stored with the schema name (xdg:schema), matched without it, so an entry the
		// desktop app wrote through a library that does not set xdg:schema is still found.
		const SecretSchemaAbi* Schema()
		{
			static SecretSchemaAbi schema = {
				"com.marketcanopy.office.Credential", 1 << 1, {{"service", 0}, {"account", 0}, {NULL, 0}}, 0,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL};
			return &schema;
		}

		const char* const kLabel = "Market Canopy Office";

		class CSecretServiceStore : public ICredentialStore
		{
		public:
			explicit CSecretServiceStore(const std::string& service) : m_service(service) {}

			EStatus Read(const std::string& account, std::string& value) override
			{
				const CLibSecret& lib = LibSecret();
				if (!lib.loaded)
					return Unavailable;
				GErrorAbi* error = NULL;
				char* secret = lib.lookup(Schema(), NULL, &error, "service", m_service.c_str(), "account", account.c_str(),
										  (const char*)NULL);
				if (error)
				{
					// No Secret Service on the bus, or the person dismissed the unlock prompt.
					lib.freeError(error);
					if (secret)
						lib.wipe(secret);
					return Unavailable;
				}
				if (!secret)
					return NotFound;
				value.assign(secret);
				lib.wipe(secret);
				return Ok;
			}

			EStatus Write(const std::string& account, const std::string& value) override
			{
				const CLibSecret& lib = LibSecret();
				if (!lib.loaded)
					return Unavailable;
				if (value.find('\0') != std::string::npos)
					return Failed;
				GErrorAbi* error = NULL;
				int ok = lib.store(Schema(), "default", kLabel, value.c_str(), NULL, &error, "service", m_service.c_str(),
								   "account", account.c_str(), (const char*)NULL);
				if (error)
				{
					lib.freeError(error);
					return Unavailable;
				}
				return ok ? Ok : Failed;
			}

			EStatus Delete(const std::string& account) override
			{
				const CLibSecret& lib = LibSecret();
				if (!lib.loaded)
					return Unavailable;
				GErrorAbi* error = NULL;
				int removed = lib.clear(Schema(), NULL, &error, "service", m_service.c_str(), "account", account.c_str(),
										(const char*)NULL);
				if (error)
				{
					lib.freeError(error);
					return Unavailable;
				}
				return removed ? Ok : NotFound;
			}

		private:
			std::string m_service;
		};
	}

	ICredentialStore* CreateOsCredentialStore(const std::string& service)
	{
		return new CSecretServiceStore(service);
	}
}

#endif
