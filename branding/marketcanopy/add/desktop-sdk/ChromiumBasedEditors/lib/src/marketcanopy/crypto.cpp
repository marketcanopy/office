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

#include "crypto.h"

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h> // linked by marketcanopy.pri (-lbcrypt)
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace NSMarketCanopy
{
	namespace
	{
		const uint32_t K[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

		inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

		void Block(uint32_t h[8], const unsigned char* p)
		{
			uint32_t w[64];
			for (int i = 0; i < 16; ++i)
				w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
			for (int i = 16; i < 64; ++i)
			{
				uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
			for (int i = 0; i < 64; ++i)
			{
				uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
				uint32_t ch = (e & f) ^ (~e & g);
				uint32_t t1 = hh + S1 + ch + K[i] + w[i];
				uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
				uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
				uint32_t t2 = S0 + maj;
				hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
		}
	}

	std::string Sha256(const std::string& data)
	{
		uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		std::string message = data;
		uint64_t bits = (uint64_t)data.size() * 8;
		message += (char)0x80;
		while (message.size() % 64 != 56)
			message += (char)0x00;
		for (int i = 7; i >= 0; --i)
			message += (char)((bits >> (i * 8)) & 0xFF);
		for (size_t off = 0; off < message.size(); off += 64)
			Block(h, (const unsigned char*)message.data() + off);
		std::string out;
		for (int i = 0; i < 8; ++i)
		{
			out += (char)((h[i] >> 24) & 0xFF);
			out += (char)((h[i] >> 16) & 0xFF);
			out += (char)((h[i] >> 8) & 0xFF);
			out += (char)(h[i] & 0xFF);
		}
		return out;
	}

	std::string Base64Url(const std::string& bytes)
	{
		static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		size_t i = 0;
		const unsigned char* p = (const unsigned char*)bytes.data();
		for (; i + 2 < bytes.size(); i += 3)
		{
			uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
			out += kAlphabet[(v >> 18) & 63];
			out += kAlphabet[(v >> 12) & 63];
			out += kAlphabet[(v >> 6) & 63];
			out += kAlphabet[v & 63];
		}
		size_t rest = bytes.size() - i;
		if (rest == 1)
		{
			uint32_t v = (uint32_t)p[i] << 16;
			out += kAlphabet[(v >> 18) & 63];
			out += kAlphabet[(v >> 12) & 63];
		}
		else if (rest == 2)
		{
			uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
			out += kAlphabet[(v >> 18) & 63];
			out += kAlphabet[(v >> 12) & 63];
			out += kAlphabet[(v >> 6) & 63];
		}
		return out;
	}

	bool RandomBytes(size_t count, std::string& out)
	{
		out.assign(count, '\0');
		if (count == 0)
			return true;
#if defined(_WIN32)
		return BCRYPT_SUCCESS(BCryptGenRandom(NULL, (PUCHAR)&out[0], (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__APPLE__)
		return SecRandomCopyBytes(kSecRandomDefault, count, &out[0]) == errSecSuccess;
#else
		// /dev/urandom rather than getrandom(2): the Linux build targets glibc older than 2.25
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return false;
		size_t got = 0;
		while (got < count)
		{
			ssize_t n = read(fd, &out[got], count - got);
			if (n <= 0)
			{
				if (n < 0 && errno == EINTR)
					continue;
				break;
			}
			got += (size_t)n;
		}
		close(fd);
		return got == count;
#endif
	}

	std::string NewUuidV4()
	{
		std::string bytes;
		if (!RandomBytes(16, bytes))
			return "";
		bytes[6] = (char)((bytes[6] & 0x0F) | 0x40);
		bytes[8] = (char)((bytes[8] & 0x3F) | 0x80);
		static const char* kHex = "0123456789abcdef";
		std::string out;
		for (int i = 0; i < 16; ++i)
		{
			if (i == 4 || i == 6 || i == 8 || i == 10)
				out += '-';
			unsigned char c = (unsigned char)bytes[i];
			out += kHex[c >> 4];
			out += kHex[c & 0xF];
		}
		return out;
	}

	std::string PkceChallenge(const std::string& verifier)
	{
		return Base64Url(Sha256(verifier));
	}

	bool NewPkce(CPkce& out)
	{
		std::string bytes;
		if (!RandomBytes(64, bytes))
			return false;
		out.verifier = Base64Url(bytes);
		out.challenge = PkceChallenge(out.verifier);
		return true;
	}
}
