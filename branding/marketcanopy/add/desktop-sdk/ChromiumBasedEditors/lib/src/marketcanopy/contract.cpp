/*
 * Market Canopy Office: the integration contract's values, readers and writers.
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

#include "contract.h"

#include <algorithm>
#include <cstdlib>
#include <stdio.h>
#include <string.h>

namespace NSMarketCanopy
{
	bool AppendUtf8Wide(const std::string& bytes, std::wstring& out);

	const char* const kClientApp = "com.marketcanopy.office";
	const char* const kScheme = "mc-office";
	const char* const kProviderId = "marketcanopy";
	const char* const kInstallationAccount = "installation";
	const char* const kAgentPluginGuid = "{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}";
	const char* const kEditorAiPluginGuid = "{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}";

	const char* ServiceName()
	{
		return "com.marketcanopy.office";
	}

	COrigins Origins()
	{
		COrigins origins;
		origins.api = "https://api.marketcanopy.com";
		origins.app = "https://app.marketcanopy.com";
		return origins;
	}

	std::string GrantAccount(const std::string& apiOrigin)
	{
		return "grant|" + apiOrigin;
	}

	namespace
	{
		bool IsLowerHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
		bool IsDigit(char c) { return c >= '0' && c <= '9'; }
		bool IsBase64UrlChar(char c)
		{
			return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || IsDigit(c) || c == '-' || c == '_';
		}

		bool StartsWith(const std::string& s, const std::string& prefix)
		{
			return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
		}

		std::string Lower(const std::string& s)
		{
			std::string out = s;
			for (size_t i = 0; i < out.size(); ++i)
				if (out[i] >= 'A' && out[i] <= 'Z')
					out[i] = (char)(out[i] - 'A' + 'a');
			return out;
		}

		int HexValue(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		// decodeURIComponent: false on a malformed escape or invalid UTF-8
		bool PercentDecode(const std::string& in, std::string& out, bool plusIsSpace)
		{
			out.clear();
			for (size_t i = 0; i < in.size(); ++i)
			{
				char c = in[i];
				if (c == '%')
				{
					if (i + 2 >= in.size())
						return false;
					int hi = HexValue(in[i + 1]);
					int lo = HexValue(in[i + 2]);
					if (hi < 0 || lo < 0)
						return false;
					out += (char)((hi << 4) | lo);
					i += 2;
				}
				else if (c == '+' && plusIsSpace)
					out += ' ';
				else
					out += c;
			}
			// the result must be UTF-8 (JSON-parsing it checks exactly that)
			Json probe;
			return Json::Parse(Json::Quote(out), probe);
		}

		// URLSearchParams-like: name=value pairs split on '&'. Lenient about a
		// malformed escape (kept as is), as URLSearchParams is.
		std::vector<std::pair<std::string, std::string> > ParseQuery(const std::string& query)
		{
			std::vector<std::pair<std::string, std::string> > params;
			size_t start = 0;
			while (start <= query.size())
			{
				size_t end = query.find('&', start);
				if (end == std::string::npos)
					end = query.size();
				std::string pair = query.substr(start, end - start);
				if (!pair.empty())
				{
					size_t eq = pair.find('=');
					std::string name = eq == std::string::npos ? pair : pair.substr(0, eq);
					std::string value = eq == std::string::npos ? "" : pair.substr(eq + 1);
					std::string decodedName, decodedValue;
					if (!PercentDecode(name, decodedName, true))
						decodedName = name;
					if (!PercentDecode(value, decodedValue, true))
						decodedValue = value;
					params.push_back(std::make_pair(decodedName, decodedValue));
				}
				start = end + 1;
			}
			return params;
		}

		bool LooksLikeCredential(const std::string& value)
		{
			if (StartsWith(value, "dsr_"))
				return true;
			if (value.size() > 7 && Lower(value.substr(0, 6)) == "bearer" &&
				(value[6] == ' ' || value[6] == '\t' || value[6] == '\n' || value[6] == '\r' || value[6] == '\f' || value[6] == '\v'))
				return true;
			// three dot-separated base64url parts, the first two at least 8 long
			size_t first = value.find('.');
			if (first == std::string::npos)
				return false;
			size_t second = value.find('.', first + 1);
			if (second == std::string::npos || value.find('.', second + 1) != std::string::npos)
				return false;
			std::string a = value.substr(0, first), b = value.substr(first + 1, second - first - 1), c = value.substr(second + 1);
			return a.size() >= 8 && b.size() >= 8 && IsBase64Url(a, 8, 100000) && IsBase64Url(b, 8, 100000) &&
				   (c.empty() || IsBase64Url(c, 1, 100000));
		}

		bool IsCredentialParam(const std::string& name)
		{
			static const char* kNames[] = {"token", "access_token", "refresh_token", "id_token", "code", "ticket",
										   "key", "apikey", "api_key", "secret", "password", "auth",
										   "authorization", "jwt", "session", "sig", "signature", NULL};
			std::string lower = Lower(name);
			for (int i = 0; kNames[i]; ++i)
				if (lower == kNames[i])
					return true;
			return false;
		}

		bool CarriesCredential(const std::vector<std::pair<std::string, std::string> >& params)
		{
			for (size_t i = 0; i < params.size(); ++i)
				if (IsCredentialParam(params[i].first) || LooksLikeCredential(params[i].second))
					return true;
			return false;
		}

		// UTF-16 length of a UTF-8 string, as JavaScript counts it
		size_t Utf16Length(const std::string& s)
		{
			size_t n = 0;
			for (size_t i = 0; i < s.size(); ++i)
			{
				unsigned char c = (unsigned char)s[i];
				if ((c & 0xC0) == 0x80)
					continue;
				n += (c >= 0xF0) ? 2 : 1;
			}
			return n;
		}

		CSchemeLink Invalid()
		{
			return CSchemeLink();
		}

		CSchemeLink Credential()
		{
			CSchemeLink link;
			link.kind = CSchemeLink::CredentialInLink;
			return link;
		}

		CSchemeLink ParseDocumentUrl(const std::string& raw)
		{
			// scheme://authority/path?query#fragment, split the way WHATWG URL does
			size_t schemeEnd = raw.find("://");
			if (schemeEnd == std::string::npos || schemeEnd == 0)
				return Invalid();
			std::string scheme = Lower(raw.substr(0, schemeEnd));
			std::string rest = raw.substr(schemeEnd + 3);
			std::string fragment;
			bool hasFragment = false;
			size_t hash = rest.find('#');
			if (hash != std::string::npos)
			{
				hasFragment = true;
				fragment = rest.substr(hash + 1);
				rest = rest.substr(0, hash);
			}
			std::string query;
			size_t question = rest.find('?');
			if (question != std::string::npos)
			{
				query = rest.substr(question + 1);
				rest = rest.substr(0, question);
			}
			size_t slash = rest.find('/');
			std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
			std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
			if (authority.empty())
				return Invalid();

			std::vector<std::pair<std::string, std::string> > params = ParseQuery(query);
			if (CarriesCredential(params))
				return Credential();

			// "#" alone still counts: URL.hash is "" then, but a fragment is never allowed here
			if (scheme != "https" || hasFragment || authority.find('@') != std::string::npos ||
				authority.find(':') != std::string::npos)
				return Invalid();
			std::string origin = "https://" + Lower(authority);
			if (origin != Origins().app)
				return Invalid();
			const std::string prefix = "/office/documents/";
			if (!StartsWith(path, prefix) || path.size() != prefix.size() + 24 || !IsObjectId(path.substr(prefix.size())))
				return Invalid();

			int organizationCount = 0;
			int desktopCount = 0;
			int placementCount = 0;
			std::string organizationId;
			for (size_t i = 0; i < params.size(); ++i)
			{
				if (params[i].first == "organizationId")
				{
					++organizationCount;
					organizationId = params[i].second;
				}
				else if (params[i].first == "desktop")
				{
					++desktopCount;
					if (params[i].second != "true")
						return Invalid();
				}
				else if (params[i].first == "placement") // an editor tab opened from a page (4.1)
				{
					++placementCount;
					if (params[i].second != "desktop")
						return Invalid();
				}
				else
					return Invalid();
			}
			if (organizationCount != 1 || desktopCount > 1 || placementCount > 1 || !IsObjectId(organizationId))
				return Invalid();

			CSchemeLink link;
			link.kind = CSchemeLink::Open;
			link.appOrigin = origin;
			link.documentId = path.substr(prefix.size());
			link.organizationId = organizationId;
			return link;
		}

		bool IsUpperCode(const std::string& s)
		{
			if (s.empty() || s.size() > 64 || !(s[0] >= 'A' && s[0] <= 'Z'))
				return false;
			for (size_t i = 1; i < s.size(); ++i)
				if (!((s[i] >= 'A' && s[i] <= 'Z') || IsDigit(s[i]) || s[i] == '_'))
					return false;
			return true;
		}

		bool MatchesTokenParam(const std::string& rest)
		{
			// /[?&](token|access_token|refresh_token|code|ticket)=/i
			static const char* kNames[] = {"token=", "access_token=", "refresh_token=", "code=", "ticket=", NULL};
			std::string lower = Lower(rest);
			for (size_t i = 0; i < lower.size(); ++i)
			{
				if (lower[i] != '?' && lower[i] != '&')
					continue;
				for (int k = 0; kNames[k]; ++k)
					if (lower.compare(i + 1, strlen(kNames[k]), kNames[k]) == 0)
						return true;
			}
			return false;
		}

		bool ParseVersionParts(const std::string& version, int parts[4])
		{
			std::string header;
			const std::string* value = &version;
			if (ClientVersionHeader(version, header))
				value = &header;
			int count = 0;
			size_t i = 0;
			while (true)
			{
				size_t start = i;
				while (i < value->size() && IsDigit((*value)[i]))
					++i;
				size_t len = i - start;
				if (len < 1 || len > 6 || count >= 4)
					return false;
				parts[count++] = atoi(value->substr(start, len).c_str());
				if (i == value->size())
					break;
				if ((*value)[i] != '.')
					return false;
				++i;
			}
			for (int k = count; k < 4; ++k)
				parts[k] = 0;
			return true;
		}

		bool Uint(const std::string& s, size_t pos, size_t len, int& out)
		{
			if (pos + len > s.size())
				return false;
			out = 0;
			for (size_t i = pos; i < pos + len; ++i)
			{
				if (!IsDigit(s[i]))
					return false;
				out = out * 10 + (s[i] - '0');
			}
			return true;
		}

		long long DaysFromCivil(int y, int m, int d)
		{
			y -= m <= 2;
			long long era = (y >= 0 ? y : y - 399) / 400;
			long long yoe = y - era * 400;
			long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + doe - 719468;
		}

		std::string JsonString(const Json& value)
		{
			return value.IsString() ? value.AsString() : std::string();
		}
	}

	bool IsObjectId(const std::string& value)
	{
		if (value.size() != 24)
			return false;
		for (size_t i = 0; i < value.size(); ++i)
			if (!IsLowerHex(value[i]))
				return false;
		return true;
	}

	bool IsUuidV4(const std::string& value)
	{
		// ^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$
		if (value.size() != 36)
			return false;
		for (size_t i = 0; i < 36; ++i)
		{
			if (i == 8 || i == 13 || i == 18 || i == 23)
			{
				if (value[i] != '-')
					return false;
			}
			else if (!IsLowerHex(value[i]))
				return false;
		}
		return value[14] == '4' && (value[19] == '8' || value[19] == '9' || value[19] == 'a' || value[19] == 'b');
	}

	bool IsBase64Url(const std::string& value, size_t minLength, size_t maxLength)
	{
		if (value.size() < minLength || value.size() > maxLength)
			return false;
		for (size_t i = 0; i < value.size(); ++i)
			if (!IsBase64UrlChar(value[i]))
				return false;
		return true;
	}

	long long ParseInstant(const std::string& s)
	{
		// YYYY-MM-DDTHH:MM:SS[.fff]Z or ±HH:MM
		int y, mo, d, h, mi, sec;
		if (s.size() < 20 || !Uint(s, 0, 4, y) || s[4] != '-' || !Uint(s, 5, 2, mo) || s[7] != '-' || !Uint(s, 8, 2, d) ||
			s[10] != 'T' || !Uint(s, 11, 2, h) || s[13] != ':' || !Uint(s, 14, 2, mi) || s[16] != ':' || !Uint(s, 17, 2, sec))
			return -1;
		if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 59)
			return -1;
		size_t i = 19;
		int ms = 0;
		if (i < s.size() && s[i] == '.')
		{
			++i;
			size_t start = i;
			while (i < s.size() && IsDigit(s[i]))
				++i;
			if (i == start)
				return -1;
			std::string frac = s.substr(start, i - start);
			while (frac.size() < 3)
				frac += '0';
			ms = atoi(frac.substr(0, 3).c_str());
		}
		long long offset = 0;
		if (i < s.size() && s[i] == 'Z' && i + 1 == s.size())
			offset = 0;
		else if (i + 6 == s.size() && (s[i] == '+' || s[i] == '-') && s[i + 3] == ':')
		{
			int oh, om;
			if (!Uint(s, i + 1, 2, oh) || !Uint(s, i + 4, 2, om) || oh > 23 || om > 59)
				return -1;
			offset = (oh * 60 + om) * 60 * 1000LL * (s[i] == '+' ? 1 : -1);
		}
		else
			return -1;
		long long days = DaysFromCivil(y, mo, d);
		return ((days * 24 + h) * 60 + mi) * 60 * 1000LL + sec * 1000LL + ms - offset;
	}

	std::string FormatInstant(long long epochMs)
	{
		long long seconds = epochMs / 1000;
		int ms = (int)(epochMs % 1000);
		long long days = seconds / 86400;
		long long rem = seconds % 86400;
		// civil from days (Howard Hinnant)
		long long z = days + 719468;
		long long era = (z >= 0 ? z : z - 146096) / 146097;
		long long doe = z - era * 146097;
		long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		long long y = yoe + era * 400;
		long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		long long mp = (5 * doy + 2) / 153;
		long long d = doy - (153 * mp + 2) / 5 + 1;
		long long m = mp + (mp < 10 ? 3 : -9);
		y += (m <= 2);
		char buffer[96]; // room for any long long, so nothing is ever cut
		snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03dZ", y, m, d, rem / 3600, (rem / 60) % 60,
				 rem % 60, ms);
		return buffer;
	}

	const char* GrantReadName(EGrantRead value)
	{
		switch (value)
		{
		case EGrantRead::Present: return "present";
		case EGrantRead::Missing: return "missing";
		case EGrantRead::Unreadable: return "unreadable";
		case EGrantRead::OtherVersion: return "other_version";
		case EGrantRead::TooLarge: return "too_large";
		case EGrantRead::Invalid: return "invalid";
		}
		return "invalid";
	}

	EGrantRead ReadGrant(const std::string* raw, const std::string& apiOrigin, CGrant& out)
	{
		if (!raw)
			return EGrantRead::Missing;
		if (raw->size() > kMaxGrantBytes)
			return EGrantRead::TooLarge;
		if (raw->size() >= 3 && (unsigned char)(*raw)[0] == 0xEF && (unsigned char)(*raw)[1] == 0xBB && (unsigned char)(*raw)[2] == 0xBF)
			return EGrantRead::Unreadable;
		Json value;
		if (!Json::Parse(*raw, value) || !value.IsObject())
			return EGrantRead::Unreadable;
		const Json& version = value.Get("version");
		if (!version.IsNumber() || version.AsNumber() != 1)
			return EGrantRead::OtherVersion;
		const Json& org = value.Get("lastActiveOrganizationId");
		bool ok = JsonString(value.Get("apiOrigin")) == apiOrigin && value.Get("apiOrigin").IsString() &&
				  IsObjectId(JsonString(value.Get("userId"))) && IsObjectId(JsonString(value.Get("deviceSessionId"))) &&
				  IsUuidV4(JsonString(value.Get("installationId"))) && StartsWith(JsonString(value.Get("refreshToken")), "dsr_") &&
				  ParseInstant(JsonString(value.Get("issuedAt"))) >= 0 &&
				  (JsonString(value.Get("writtenBy")) == "desktop" || JsonString(value.Get("writtenBy")) == "office") &&
				  value.Has("lastActiveOrganizationId") && (org.IsNull() || IsObjectId(JsonString(org)));
		if (!ok)
			return EGrantRead::Invalid;
		out.apiOrigin = apiOrigin;
		out.userId = value.Get("userId").AsString();
		out.deviceSessionId = value.Get("deviceSessionId").AsString();
		out.installationId = value.Get("installationId").AsString();
		out.refreshToken = value.Get("refreshToken").AsString();
		out.issuedAt = value.Get("issuedAt").AsString();
		out.writtenBy = value.Get("writtenBy").AsString();
		out.lastActiveOrganizationId = org.IsNull() ? std::string() : org.AsString();
		return EGrantRead::Present;
	}

	std::string WriteGrant(const CGrant& grant)
	{
		Json value = Json::MakeObject();
		value.Set("version", Json::MakeNumber(1));
		value.Set("apiOrigin", Json::MakeString(grant.apiOrigin));
		value.Set("userId", Json::MakeString(grant.userId));
		value.Set("deviceSessionId", Json::MakeString(grant.deviceSessionId));
		value.Set("installationId", Json::MakeString(grant.installationId));
		value.Set("refreshToken", Json::MakeString(grant.refreshToken));
		value.Set("issuedAt", Json::MakeString(grant.issuedAt));
		value.Set("writtenBy", Json::MakeString(grant.writtenBy));
		value.Set("lastActiveOrganizationId",
				  grant.lastActiveOrganizationId.empty() ? Json() : Json::MakeString(grant.lastActiveOrganizationId));
		return value.Dump();
	}

	bool DesktopMayWrite(EGrantRead state, const CGrant& current, const std::string& signedInUserId)
	{
		if (state != EGrantRead::Present)
			return true;
		return current.userId != signedInUserId;
	}

	bool MayDelete(EGrantRead state, const CGrant& current, const std::string& revokedDeviceSessionId)
	{
		return state == EGrantRead::Present && current.deviceSessionId == revokedDeviceSessionId;
	}

	bool ReadInstallation(const std::string& raw, std::string& installationId)
	{
		Json value;
		if (!Json::Parse(raw, value) || !value.IsObject())
			return false;
		if (!value.Get("version").IsNumber() || value.Get("version").AsNumber() != 1)
			return false;
		std::string id = JsonString(value.Get("installationId"));
		if (!IsUuidV4(id))
			return false;
		installationId = id;
		return true;
	}

	std::string WriteInstallation(const std::string& installationId)
	{
		Json value = Json::MakeObject();
		value.Set("version", Json::MakeNumber(1));
		value.Set("installationId", Json::MakeString(installationId));
		return value.Dump();
	}

	bool IsSchemeUrl(const std::string& input)
	{
		std::string prefix = std::string(kScheme) + ":";
		return input.size() >= prefix.size() && Lower(input.substr(0, prefix.size())) == prefix;
	}

	CSchemeLink ParseSchemeUrl(const std::string& input)
	{
		if (input.size() > 4096)
			return Invalid();
		for (size_t i = 0; i < input.size(); ++i)
			if ((unsigned char)input[i] < 0x20)
				return Invalid();
		const std::string prefix = std::string(kScheme) + "://";
		if (!StartsWith(input, prefix))
			return Invalid();
		std::string rest = input.substr(prefix.size());
		for (size_t pos = 0; (pos = rest.find('%', pos)) != std::string::npos;)
		{
			if (pos + 2 < rest.size() && rest[pos + 1] == '7' && (rest[pos + 2] == 'C' || rest[pos + 2] == 'c'))
				rest.replace(pos, 3, "|");
			else
				++pos;
		}

		if (StartsWith(rest, "open|"))
		{
			std::vector<std::string> fields;
			size_t start = 0;
			while (true)
			{
				size_t bar = rest.find('|', start);
				fields.push_back(rest.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
				if (bar == std::string::npos)
					break;
				start = bar + 1;
			}
			if (fields.size() != 4 || fields[1] != "n")
			{
				if (MatchesTokenParam(rest))
					return Credential();
				return Invalid();
			}
			CSchemeLink link = ParseDocumentUrl(fields[3]);
			if (link.kind != CSchemeLink::Open)
				return link;
			std::string name;
			if (!PercentDecode(fields[2], name, false))
				return Invalid();
			size_t length = Utf16Length(name);
			if (length < 1 || length > 255 || name.find('|') != std::string::npos)
				return Invalid();
			for (size_t i = 0; i < name.size(); ++i)
				if ((unsigned char)name[i] < 0x20)
					return Invalid();
			link.name = name;
			return link;
		}

		const std::string callback = "auth/callback?";
		if (StartsWith(rest, callback))
		{
			std::vector<std::pair<std::string, std::string> > params = ParseQuery(rest.substr(callback.size()));
			std::vector<std::string> keys;
			std::string code, state, error;
			bool haveState = false;
			for (size_t i = 0; i < params.size(); ++i)
			{
				keys.push_back(params[i].first);
				if (params[i].first == "code" && code.empty()) code = params[i].second;
				if (params[i].first == "error" && error.empty()) error = params[i].second;
				if (params[i].first == "state" && !haveState) { state = params[i].second; haveState = true; }
			}
			std::sort(keys.begin(), keys.end());
			if (!IsBase64Url(state, 43, 43))
				return Invalid();
			std::string joined;
			for (size_t i = 0; i < keys.size(); ++i)
				joined += (i ? "," : "") + keys[i];
			CSchemeLink link;
			link.state = state;
			if (joined == "code,state")
			{
				if (!IsBase64Url(code, 43, 43))
					return Invalid();
				link.kind = CSchemeLink::AuthCallback;
				link.code = code;
				return link;
			}
			if (joined == "error,state")
			{
				if (!IsUpperCode(error))
					return Invalid();
				link.kind = CSchemeLink::AuthError;
				link.error = error;
				return link;
			}
			return Invalid();
		}

		// //action|..., //command/..., plugin-store links and anything else
		return Invalid();
	}

	bool ClientVersionHeader(const std::string& release, std::string& header)
	{
		// ^(\d{1,6})\.(\d{1,6})\.(\d{1,6})-mc\.(\d{1,6})$
		std::string parts[4];
		size_t i = 0;
		for (int k = 0; k < 4; ++k)
		{
			size_t start = i;
			while (i < release.size() && IsDigit(release[i]))
				++i;
			if (i - start < 1 || i - start > 6)
				return false;
			parts[k] = release.substr(start, i - start);
			if (k < 2)
			{
				if (i >= release.size() || release[i] != '.')
					return false;
				++i;
			}
			else if (k == 2)
			{
				if (release.compare(i, 4, "-mc.") != 0)
					return false;
				i += 4;
			}
		}
		if (i != release.size())
			return false;
		header = parts[0] + "." + parts[1] + "." + parts[2] + "." + parts[3];
		return true;
	}

	int CompareVersions(const std::string& left, const std::string& right)
	{
		int a[4], b[4];
		if (!ParseVersionParts(left, a) || !ParseVersionParts(right, b))
			return 2;
		for (int i = 0; i < 4; ++i)
			if (a[i] != b[i])
				return a[i] < b[i] ? -1 : 1;
		return 0;
	}

	CApiResult ReadEnvelope(int status, const std::string& body, bool networkError)
	{
		CApiResult result;
		result.status = status;
		if (networkError)
		{
			result.kind = CApiResult::Network;
			result.code = "NETWORK_UNAVAILABLE";
			return result;
		}
		Json value;
		bool parsed = Json::Parse(body, value) && value.IsObject();
		if (parsed && status >= 200 && status < 300 && value.Get("success").IsBool() && value.Get("success").AsBool())
		{
			result.kind = CApiResult::Ok;
			result.data = value.Get("data");
			return result;
		}
		result.kind = CApiResult::Error;
		if (parsed && value.Get("error").IsObject())
		{
			result.code = JsonString(value.Get("error").Get("code"));
			result.message = JsonString(value.Get("error").Get("message"));
			result.details = value.Get("error").Get("details");
		}
		if (result.code.empty())
		{
			// a gateway, proxy or captive portal answered: name it by status
			if (status == 401) result.code = "UNAUTHORIZED";
			else if (status == 426) result.code = "CLIENT_UPGRADE_REQUIRED";
			else if (status == 0 || status >= 500 || !parsed) result.code = "NETWORK_UNAVAILABLE";
			else result.code = "UNEXPECTED_RESPONSE";
		}
		return result;
	}

	bool ReadBootstrap(const Json& data, CBootstrap& out)
	{
		if (!data.IsObject() || JsonString(data.Get("contractVersion")) != "1")
			return false;
		const Json& user = data.Get("user");
		const Json& office = data.Get("office");
		const Json& docs = data.Get("docs");
		const Json& ai = data.Get("ai");
		if (!user.IsObject() || !office.IsObject() || !docs.IsObject() || !ai.IsObject() || !data.Get("organizations").IsArray())
			return false;
		CBootstrap b;
		b.userId = JsonString(user.Get("id"));
		b.email = JsonString(user.Get("email"));
		b.displayName = JsonString(user.Get("displayName"));
		if (!IsObjectId(b.userId))
			return false;
		const Json& organizations = data.Get("organizations");
		for (size_t i = 0; i < organizations.Size(); ++i)
		{
			COrganization organization;
			organization.id = JsonString(organizations.At(i).Get("id"));
			organization.name = JsonString(organizations.At(i).Get("name"));
			if (!IsObjectId(organization.id))
				return false;
			b.organizations.push_back(organization);
		}
		b.activeOrganizationId = JsonString(data.Get("activeOrganizationId"));
		b.minimumVersion = JsonString(office.Get("minimumVersion"));
		b.latestVersion = JsonString(office.Get("latestVersion"));
		b.updateRequired = office.Get("updateRequired").IsBool() && office.Get("updateRequired").AsBool();
		b.docsEnabled = docs.Get("enabled").IsBool() && docs.Get("enabled").AsBool();
		b.docsAppOrigin = JsonString(docs.Get("appOrigin"));
		b.connectPath = JsonString(docs.Get("connectPath"));
		b.documentsPath = JsonString(docs.Get("documentsPath"));
		b.aiEnabled = ai.Get("enabled").IsBool() && ai.Get("enabled").AsBool();
		b.aiDisabledReason = JsonString(ai.Get("disabledReason"));
		b.aiBaseUrl = JsonString(ai.Get("baseUrl"));
		b.defaultModel = JsonString(ai.Get("defaultModel"));
		const Json& models = ai.Get("models");
		for (size_t i = 0; i < models.Size(); ++i)
		{
			CModel model;
			model.id = JsonString(models.At(i).Get("id"));
			model.name = JsonString(models.At(i).Get("name"));
			const Json& capabilities = models.At(i).Get("capabilities");
			for (size_t k = 0; k < capabilities.Size(); ++k)
				model.capabilities.push_back(JsonString(capabilities.At(k)));
			if (!model.id.empty())
				b.models.push_back(model);
		}
		// 8.1: below the minimum version neither docs nor AI is on, whatever the flags say
		if (b.updateRequired)
		{
			b.docsEnabled = false;
			b.aiEnabled = false;
		}
		// docs open only on an allowed app origin (4.1)
		if (b.docsAppOrigin != Origins().app)
			b.docsEnabled = false;
		if (b.connectPath != "/office/connect")
			b.docsEnabled = false;
		// AI needs an https gateway and a model
		if (b.aiEnabled && (!StartsWith(b.aiBaseUrl, "https://") || b.models.empty() || b.defaultModel.empty()))
			b.aiEnabled = false;
		out = b;
		return true;
	}

	std::string AiSessionReady(const std::string& baseUrl, const std::string& accessToken, const std::string& expiresAt,
							   const std::string& organizationId, const std::vector<CModel>& models,
							   const std::string& defaultModel)
	{
		Json value = Json::MakeObject();
		value.Set("state", Json::MakeString("ready"));
		value.Set("baseUrl", Json::MakeString(baseUrl));
		value.Set("accessToken", Json::MakeString(accessToken));
		value.Set("expiresAt", Json::MakeString(expiresAt));
		value.Set("organizationId", Json::MakeString(organizationId));
		Json list = Json::MakeArray();
		for (size_t i = 0; i < models.size(); ++i)
		{
			Json model = Json::MakeObject();
			model.Set("id", Json::MakeString(models[i].id));
			model.Set("name", Json::MakeString(models[i].name));
			Json capabilities = Json::MakeArray();
			for (size_t k = 0; k < models[i].capabilities.size(); ++k)
				capabilities.Push(Json::MakeString(models[i].capabilities[k]));
			model.Set("capabilities", capabilities);
			list.Push(model);
		}
		value.Set("models", list);
		value.Set("defaultModel", Json::MakeString(defaultModel));
		return value.Dump();
	}

	std::string AiSessionState(const char* state)
	{
		Json value = Json::MakeObject();
		value.Set("state", Json::MakeString(state));
		return value.Dump();
	}

	namespace
	{
		std::wstring Normalize(const std::wstring& path)
		{
			std::wstring out = path;
			for (size_t i = 0; i < out.size(); ++i)
				if (out[i] == L'\\')
					out[i] = L'/';
			while (out.size() > 1 && out[out.size() - 1] == L'/')
				out.erase(out.size() - 1);
#if defined(_WIN32)
			if (out.size() > 2 && out[0] == L'/' && out[2] == L':')
				out.erase(0, 1);
			for (size_t i = 0; i < out.size(); ++i)
				if (out[i] >= L'A' && out[i] <= L'Z')
					out[i] = (wchar_t)(out[i] - L'A' + L'a');
#else
			if (!out.empty() && out[0] != L'/')
				out = L"/" + out;
#endif
			return out;
		}

		bool HasDotSegment(const std::wstring& path)
		{
			size_t start = 0;
			while (start <= path.size())
			{
				size_t end = path.find(L'/', start);
				if (end == std::wstring::npos)
					end = path.size();
				std::wstring segment = path.substr(start, end - start);
				if (segment == L"." || segment == L"..")
					return true;
				start = end + 1;
			}
			return false;
		}

		bool DecodeWide(const std::wstring& in, std::wstring& out)
		{
			// %XX are UTF-8 bytes; anything above 0x7F in `in` is kept as is
			std::string bytes;
			std::wstring result;
			for (size_t i = 0; i < in.size(); ++i)
			{
				wchar_t c = in[i];
				if (c == L'%' && i + 2 < in.size())
				{
					int hi = HexValue((char)in[i + 1]);
					int lo = HexValue((char)in[i + 2]);
					if (in[i + 1] > 0x7F || in[i + 2] > 0x7F || hi < 0 || lo < 0)
						return false;
					int byte = (hi << 4) | lo;
					if (byte == 0)
						return false;
					bytes += (char)byte;
					i += 2;
					continue;
				}
				if (!bytes.empty())
				{
					if (!AppendUtf8Wide(bytes, result))
						return false;
					bytes.clear();
				}
				result += c;
			}
			if (!bytes.empty() && !AppendUtf8Wide(bytes, result))
				return false;
			out = result;
			return true;
		}
	}

	bool AppendUtf8Wide(const std::string& bytes, std::wstring& out)
	{
		Json probe;
		if (!Json::Parse(Json::Quote(bytes), probe))
			return false;
		for (size_t i = 0; i < bytes.size();)
		{
			unsigned char c = (unsigned char)bytes[i];
			unsigned long cp;
			int extra;
			if (c < 0x80) { cp = c; extra = 0; }
			else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
			else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
			else { cp = c & 0x07; extra = 3; }
			for (int k = 1; k <= extra; ++k)
				cp = (cp << 6) | ((unsigned char)bytes[i + k] & 0x3F);
			i += extra + 1;
			if (sizeof(wchar_t) == 2 && cp >= 0x10000)
			{
				cp -= 0x10000;
				out += (wchar_t)(0xD800 + (cp >> 10));
				out += (wchar_t)(0xDC00 + (cp & 0x3FF));
			}
			else
				out += (wchar_t)cp;
		}
		return true;
	}

	bool IsStartPageUrl(const std::wstring& url, const std::wstring& localEditorsPath)
	{
		// macOS loads the start page from a file URL
		if (url.compare(0, 8, L"file:///") == 0)
			return true;
		// Windows and Linux load <application folder>/index.html through the
		// plugin scheme (desktop-apps HTML_SCHEME); the editors live at
		// <application folder>/editors/web-apps/apps/api/documents/index.html.
		const std::wstring scheme = L"onlyoffice://plugin/";
		const std::wstring editors = L"/editors/web-apps/apps/api/documents/index.html";
		if (url.compare(0, scheme.size(), scheme) != 0)
			return false;
		std::wstring root = Normalize(localEditorsPath);
		if (root.size() <= editors.size() || root.compare(root.size() - editors.size(), editors.size(), editors) != 0)
			return false;
		root.erase(root.size() - editors.size());
		std::wstring path = url.substr(scheme.size());
		size_t cut = path.find_first_of(L"?#");
		if (cut != std::wstring::npos)
			path = path.substr(0, cut);
		std::wstring decoded;
		if (!DecodeWide(path, decoded) || decoded.empty())
			return false;
		decoded = Normalize(decoded);
		if (HasDotSegment(decoded) || HasDotSegment(root))
			return false;
		return decoded == root + L"/index.html";
	}

	bool IsAiSessionFrame(const std::wstring& frameUrl, const std::wstring& systemPluginsDir)
	{
		// The start window loads plugins through the plugin scheme; an editor
		// loads its system plugins from file URLs (desktop-sdk plugins.h
		// serializePlugins: "file:///" + folder, or "file://" + "/folder").
		const std::wstring scheme = L"onlyoffice://plugin/";
		const std::wstring file = L"file:///";
		if (systemPluginsDir.empty())
			return false;
		std::wstring path;
		if (frameUrl.compare(0, scheme.size(), scheme) == 0)
			path = frameUrl.substr(scheme.size());
		else if (frameUrl.compare(0, file.size(), file) == 0)
			path = frameUrl.substr(file.size() - 1); // keeps the leading slash
		else
			return false;
		size_t cut = path.find_first_of(L"?#");
		if (cut != std::wstring::npos)
			path = path.substr(0, cut);
		std::wstring decoded;
		if (!DecodeWide(path, decoded) || decoded.empty())
			return false;
		decoded = Normalize(decoded);
		std::wstring root = Normalize(systemPluginsDir);
		if (HasDotSegment(decoded) || HasDotSegment(root))
			return false;
		static const wchar_t* kEntries[][2] = {
			{L"{9DC93CDB-B576-4F0C-B55E-FCC9C48DD777}", L"index.html"},
			{L"{9DC93CDB-B576-4F0C-B55E-FCC9C48DD007}", L"index.html"},
		};
		for (size_t i = 0; i < sizeof(kEntries) / sizeof(kEntries[0]); ++i)
		{
			std::wstring expected = Normalize(root + L"/" + kEntries[i][0] + L"/" + kEntries[i][1]);
			if (decoded == expected)
				return true;
		}
		return false;
	}
}
