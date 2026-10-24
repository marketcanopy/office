/*
 * Market Canopy Office: the login keychain store (SPEC 5.3, macOS).
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

// Security.framework's SecItem C API, part of the OS. The item's access list
// is left as the keychain creates it: the other app gets one OS access prompt
// the first time it reads (SPEC 5.3, ADR 0501 decision 3).

#ifdef __APPLE__

#include "platform.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace NSMarketCanopy
{
	namespace
	{
		CFStringRef String(const std::string& utf8)
		{
			return CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)utf8.data(), (CFIndex)utf8.size(),
										   kCFStringEncodingUTF8, false);
		}

		ICredentialStore::EStatus FromStatus(OSStatus status)
		{
			switch (status)
			{
			case errSecSuccess:
				return ICredentialStore::Ok;
			case errSecItemNotFound:
				return ICredentialStore::NotFound;
			case errSecInteractionNotAllowed: // a locked keychain with no UI
			case errSecAuthFailed:
			case errSecUserCanceled:          // the person declined the prompt
			case errSecNoSuchKeychain:
			case errSecNotAvailable:
				return ICredentialStore::Unavailable;
			default:
				return ICredentialStore::Failed;
			}
		}

		class CKeychainStore : public ICredentialStore
		{
		public:
			explicit CKeychainStore(const std::string& service) : m_service(service) {}

			EStatus Read(const std::string& account, std::string& value) override
			{
				CFMutableDictionaryRef query = Query(account);
				if (!query)
					return Failed;
				CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
				CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
				CFTypeRef result = NULL;
				OSStatus status = SecItemCopyMatching(query, &result);
				CFRelease(query);
				if (status != errSecSuccess)
					return FromStatus(status);
				if (!result || CFGetTypeID(result) != CFDataGetTypeID())
				{
					if (result)
						CFRelease(result);
					return Failed;
				}
				CFDataRef data = (CFDataRef)result;
				value.assign((const char*)CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
				CFRelease(result);
				return Ok;
			}

			EStatus Write(const std::string& account, const std::string& value) override
			{
				CFMutableDictionaryRef query = Query(account);
				if (!query)
					return Failed;
				CFDataRef data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)value.data(), (CFIndex)value.size());
				CFStringRef label = String("Market Canopy Office");
				CFMutableDictionaryRef update = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
																		  &kCFTypeDictionaryValueCallBacks);
				CFDictionarySetValue(update, kSecValueData, data);
				CFDictionarySetValue(update, kSecAttrLabel, label);
				OSStatus status = SecItemUpdate(query, update);
				if (status == errSecItemNotFound)
				{
					CFDictionarySetValue(query, kSecValueData, data);
					CFDictionarySetValue(query, kSecAttrLabel, label);
					status = SecItemAdd(query, NULL);
				}
				CFRelease(update);
				CFRelease(label);
				CFRelease(data);
				CFRelease(query);
				return FromStatus(status);
			}

			EStatus Delete(const std::string& account) override
			{
				CFMutableDictionaryRef query = Query(account);
				if (!query)
					return Failed;
				OSStatus status = SecItemDelete(query);
				CFRelease(query);
				return FromStatus(status);
			}

		private:
			std::string m_service;

			CFMutableDictionaryRef Query(const std::string& account) const
			{
				CFStringRef service = String(m_service);
				CFStringRef accountName = String(account);
				if (!service || !accountName)
				{
					if (service)
						CFRelease(service);
					if (accountName)
						CFRelease(accountName);
					return NULL;
				}
				CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
																		 &kCFTypeDictionaryValueCallBacks);
				CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
				CFDictionarySetValue(query, kSecAttrService, service);
				CFDictionarySetValue(query, kSecAttrAccount, accountName);
				CFRelease(service);
				CFRelease(accountName);
				return query;
			}
		};
	}

	ICredentialStore* CreateOsCredentialStore(const std::string& service)
	{
		return new CKeychainStore(service);
	}
}

#endif
