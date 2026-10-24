/*
 * Market Canopy Office: SHA-256, base64url and OS randomness for PKCE.
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

#ifndef MARKETCANOPY_CRYPTO_H
#define MARKETCANOPY_CRYPTO_H

#include <string>

namespace NSMarketCanopy
{
	// The 32-byte SHA-256 digest of `data` (FIPS 180-4).
	std::string Sha256(const std::string& data);

	// RFC 4648 section 5, without padding.
	std::string Base64Url(const std::string& bytes);

	// `count` bytes from the OS CSPRNG (BCryptGenRandom, SecRandomCopyBytes,
	// or /dev/urandom). false when the OS gives fewer.
	bool RandomBytes(size_t count, std::string& out);

	// A lower-case RFC 4122 version 4 UUID, or "" when no randomness is available.
	std::string NewUuidV4();

	// RFC 7636: a 64-byte verifier (86 characters) and its S256 challenge.
	struct CPkce
	{
		std::string verifier;
		std::string challenge;
	};
	bool NewPkce(CPkce& out);
	std::string PkceChallenge(const std::string& verifier);
}

#endif // MARKETCANOPY_CRYPTO_H
