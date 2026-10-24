/*
 * Market Canopy Office: the Credential Manager store (SPEC 5.3, Windows).
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

// wincred.h (advapi32), part of the OS: no library is added.

#ifdef _WIN32

#include "platform.h"

#include <windows.h>
#include <wincred.h>

namespace NSMarketCanopy
{
	namespace
	{
		std::wstring Wide(const std::string& utf8)
		{
			if (utf8.empty())
				return std::wstring();
			int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), NULL, 0);
			if (count <= 0)
				return std::wstring();
			std::wstring out((size_t)count, L'\0');
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), &out[0], count);
			return out;
		}

		ICredentialStore::EStatus FromError(DWORD error)
		{
			switch (error)
			{
			case ERROR_NOT_FOUND:
				return ICredentialStore::NotFound;
			case ERROR_NO_SUCH_LOGON_SESSION: // a service or a session with no credential set
			case ERROR_ACCESS_DENIED:
				return ICredentialStore::Unavailable;
			default:
				return ICredentialStore::Failed;
			}
		}

		class CCredentialManagerStore : public ICredentialStore
		{
		public:
			explicit CCredentialManagerStore(const std::string& service) : m_service(service) {}

			EStatus Read(const std::string& account, std::string& value) override
			{
				std::wstring target = Target(account);
				PCREDENTIALW credential = NULL;
				if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
					return FromError(GetLastError());
				value.assign((const char*)credential->CredentialBlob, credential->CredentialBlobSize);
				SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
				CredFree(credential);
				return Ok;
			}

			EStatus Write(const std::string& account, const std::string& value) override
			{
				if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
					return Failed;
				std::wstring target = Target(account);
				std::wstring userName = Wide(account);
				CREDENTIALW credential;
				ZeroMemory(&credential, sizeof(credential));
				credential.Type = CRED_TYPE_GENERIC;
				credential.TargetName = &target[0];
				credential.UserName = &userName[0];
				credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
				credential.CredentialBlobSize = (DWORD)value.size();
				credential.CredentialBlob = (LPBYTE)value.data();
				if (!CredWriteW(&credential, 0))
					return FromError(GetLastError());
				return Ok;
			}

			EStatus Delete(const std::string& account) override
			{
				std::wstring target = Target(account);
				if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
					return FromError(GetLastError());
				return Ok;
			}

		private:
			std::string m_service;

			std::wstring Target(const std::string& account) const
			{
				return Wide(m_service + ":" + account);
			}
		};
	}

	ICredentialStore* CreateOsCredentialStore(const std::string& service)
	{
		return new CCredentialManagerStore(service);
	}
}

#endif
