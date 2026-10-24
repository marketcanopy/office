/*
 * Market Canopy Office: a small strict JSON value for the account module.
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

#include "json.h"

#include <cmath>
#include <locale>
#include <sstream>

namespace NSMarketCanopy
{
	namespace
	{
		const int kMaxDepth = 64;

		class CParser
		{
		public:
			explicit CParser(const std::string& text) : m_text(text), m_pos(0) {}

			bool ParseDocument(Json& out)
			{
				SkipSpace();
				if (!ParseValue(out, 0))
					return false;
				SkipSpace();
				return m_pos == m_text.size();
			}

		private:
			const std::string& m_text;
			size_t m_pos;

			void SkipSpace()
			{
				while (m_pos < m_text.size())
				{
					char c = m_text[m_pos];
					if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
						break;
					++m_pos;
				}
			}

			bool Literal(const char* word)
			{
				size_t n = 0;
				while (word[n])
					++n;
				if (m_text.compare(m_pos, n, word) != 0)
					return false;
				m_pos += n;
				return true;
			}

			bool ParseValue(Json& out, int depth)
			{
				if (depth > kMaxDepth || m_pos >= m_text.size())
					return false;
				char c = m_text[m_pos];
				switch (c)
				{
				case 'n': if (!Literal("null")) return false; out = Json(); return true;
				case 't': if (!Literal("true")) return false; out = Json::MakeBool(true); return true;
				case 'f': if (!Literal("false")) return false; out = Json::MakeBool(false); return true;
				case '"':
				{
					std::string s;
					if (!ParseString(s))
						return false;
					out = Json::MakeString(s);
					return true;
				}
				case '[': return ParseArray(out, depth);
				case '{': return ParseObject(out, depth);
				default:
					if (c == '-' || (c >= '0' && c <= '9'))
						return ParseNumber(out);
					return false;
				}
			}

			bool ParseNumber(Json& out)
			{
				size_t start = m_pos;
				if (m_text[m_pos] == '-')
					++m_pos;
				if (m_pos >= m_text.size())
					return false;
				if (m_text[m_pos] == '0')
					++m_pos;
				else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9')
					while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
				else
					return false;
				if (m_pos < m_text.size() && m_text[m_pos] == '.')
				{
					++m_pos;
					size_t digits = m_pos;
					while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
					if (m_pos == digits)
						return false;
				}
				if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
				{
					++m_pos;
					if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-'))
						++m_pos;
					size_t digits = m_pos;
					while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
					if (m_pos == digits)
						return false;
				}
				std::istringstream stream(m_text.substr(start, m_pos - start));
				stream.imbue(std::locale::classic());
				double value = 0;
				stream >> value;
				if (stream.fail() || !std::isfinite(value))
					return false;
				out = Json::MakeNumber(value);
				return true;
			}

			static void AppendUtf8(std::string& s, unsigned long cp)
			{
				if (cp < 0x80)
					s += (char)cp;
				else if (cp < 0x800)
				{
					s += (char)(0xC0 | (cp >> 6));
					s += (char)(0x80 | (cp & 0x3F));
				}
				else if (cp < 0x10000)
				{
					s += (char)(0xE0 | (cp >> 12));
					s += (char)(0x80 | ((cp >> 6) & 0x3F));
					s += (char)(0x80 | (cp & 0x3F));
				}
				else
				{
					s += (char)(0xF0 | (cp >> 18));
					s += (char)(0x80 | ((cp >> 12) & 0x3F));
					s += (char)(0x80 | ((cp >> 6) & 0x3F));
					s += (char)(0x80 | (cp & 0x3F));
				}
			}

			bool Hex4(unsigned long& out)
			{
				if (m_pos + 4 > m_text.size())
					return false;
				out = 0;
				for (int i = 0; i < 4; ++i)
				{
					char c = m_text[m_pos++];
					out <<= 4;
					if (c >= '0' && c <= '9') out |= (unsigned long)(c - '0');
					else if (c >= 'a' && c <= 'f') out |= (unsigned long)(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') out |= (unsigned long)(c - 'A' + 10);
					else return false;
				}
				return true;
			}

			// One UTF-8 sequence starting at m_pos, validated (no overlongs, no surrogates).
			bool CopyUtf8(std::string& s)
			{
				unsigned char c = (unsigned char)m_text[m_pos];
				int extra = 0;
				unsigned long cp = 0;
				if (c >= 0xC2 && c <= 0xDF) { extra = 1; cp = c & 0x1F; }
				else if (c >= 0xE0 && c <= 0xEF) { extra = 2; cp = c & 0x0F; }
				else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07; }
				else return false;
				if (m_pos + extra >= m_text.size())
					return false;
				for (int i = 1; i <= extra; ++i)
				{
					unsigned char d = (unsigned char)m_text[m_pos + i];
					if ((d & 0xC0) != 0x80)
						return false;
					cp = (cp << 6) | (d & 0x3F);
				}
				if ((extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
					return false;
				if (cp >= 0xD800 && cp <= 0xDFFF)
					return false;
				s.append(m_text, m_pos, extra + 1);
				m_pos += extra + 1;
				return true;
			}

			bool ParseString(std::string& s)
			{
				++m_pos; // opening quote
				while (m_pos < m_text.size())
				{
					unsigned char c = (unsigned char)m_text[m_pos];
					if (c == '"')
					{
						++m_pos;
						return true;
					}
					if (c < 0x20)
						return false;
					if (c == '\\')
					{
						++m_pos;
						if (m_pos >= m_text.size())
							return false;
						char e = m_text[m_pos++];
						switch (e)
						{
						case '"': s += '"'; break;
						case '\\': s += '\\'; break;
						case '/': s += '/'; break;
						case 'b': s += '\b'; break;
						case 'f': s += '\f'; break;
						case 'n': s += '\n'; break;
						case 'r': s += '\r'; break;
						case 't': s += '\t'; break;
						case 'u':
						{
							unsigned long cp = 0;
							if (!Hex4(cp))
								return false;
							if (cp >= 0xD800 && cp <= 0xDBFF)
							{
								unsigned long low = 0;
								if (m_pos + 2 > m_text.size() || m_text[m_pos] != '\\' || m_text[m_pos + 1] != 'u')
									return false;
								m_pos += 2;
								if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF)
									return false;
								cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
							}
							else if (cp >= 0xDC00 && cp <= 0xDFFF)
								return false;
							AppendUtf8(s, cp);
							break;
						}
						default:
							return false;
						}
						continue;
					}
					if (c < 0x80)
					{
						s += (char)c;
						++m_pos;
						continue;
					}
					if (!CopyUtf8(s))
						return false;
				}
				return false;
			}

			bool ParseArray(Json& out, int depth)
			{
				++m_pos;
				out = Json::MakeArray();
				SkipSpace();
				if (m_pos < m_text.size() && m_text[m_pos] == ']')
				{
					++m_pos;
					return true;
				}
				while (true)
				{
					SkipSpace();
					Json item;
					if (!ParseValue(item, depth + 1))
						return false;
					out.Push(item);
					SkipSpace();
					if (m_pos >= m_text.size())
						return false;
					if (m_text[m_pos] == ',')
					{
						++m_pos;
						continue;
					}
					if (m_text[m_pos] == ']')
					{
						++m_pos;
						return true;
					}
					return false;
				}
			}

			bool ParseObject(Json& out, int depth)
			{
				++m_pos;
				out = Json::MakeObject();
				SkipSpace();
				if (m_pos < m_text.size() && m_text[m_pos] == '}')
				{
					++m_pos;
					return true;
				}
				while (true)
				{
					SkipSpace();
					if (m_pos >= m_text.size() || m_text[m_pos] != '"')
						return false;
					std::string key;
					if (!ParseString(key))
						return false;
					if (out.Has(key))
						return false;
					SkipSpace();
					if (m_pos >= m_text.size() || m_text[m_pos] != ':')
						return false;
					++m_pos;
					SkipSpace();
					Json value;
					if (!ParseValue(value, depth + 1))
						return false;
					out.Set(key, value);
					SkipSpace();
					if (m_pos >= m_text.size())
						return false;
					if (m_text[m_pos] == ',')
					{
						++m_pos;
						continue;
					}
					if (m_text[m_pos] == '}')
					{
						++m_pos;
						return true;
					}
					return false;
				}
			}
		};

		const Json& NullJson()
		{
			static const Json kNull;
			return kNull;
		}
	}

	Json Json::MakeBool(bool value) { Json j; j.m_type = Bool; j.m_bool = value; return j; }
	Json Json::MakeNumber(double value) { Json j; j.m_type = Number; j.m_number = value; return j; }
	Json Json::MakeString(const std::string& value) { Json j; j.m_type = String; j.m_string = value; return j; }
	Json Json::MakeArray() { Json j; j.m_type = Array; return j; }
	Json Json::MakeObject() { Json j; j.m_type = Object; return j; }

	bool Json::Parse(const std::string& text, Json& out)
	{
		CParser parser(text);
		Json value;
		if (!parser.ParseDocument(value))
			return false;
		out = value;
		return true;
	}

	const Json& Json::At(size_t index) const
	{
		if (m_type != Array || index >= m_items.size())
			return NullJson();
		return m_items[index];
	}

	void Json::Push(const Json& value)
	{
		if (m_type == Array)
			m_items.push_back(value);
	}

	bool Json::Has(const std::string& key) const
	{
		if (m_type != Object)
			return false;
		for (size_t i = 0; i < m_members.size(); ++i)
			if (m_members[i].first == key)
				return true;
		return false;
	}

	const Json& Json::Get(const std::string& key) const
	{
		if (m_type == Object)
			for (size_t i = 0; i < m_members.size(); ++i)
				if (m_members[i].first == key)
					return m_members[i].second;
		return NullJson();
	}

	void Json::Set(const std::string& key, const Json& value)
	{
		if (m_type != Object)
			return;
		for (size_t i = 0; i < m_members.size(); ++i)
		{
			if (m_members[i].first == key)
			{
				m_members[i].second = value;
				return;
			}
		}
		m_members.push_back(std::make_pair(key, value));
	}

	std::string Json::Quote(const std::string& value)
	{
		static const char* kHex = "0123456789abcdef";
		std::string out = "\"";
		for (size_t i = 0; i < value.size(); ++i)
		{
			unsigned char c = (unsigned char)value[i];
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				// '<' and '>' are escaped so a value can be put inside a script without ending it
				if (c < 0x20 || c == '<' || c == '>' || c == 0x7F)
				{
					out += "\\u00";
					out += kHex[c >> 4];
					out += kHex[c & 0xF];
				}
				else
					out += (char)c;
			}
		}
		// U+2028 and U+2029 end a JavaScript string literal in older engines
		std::string::size_type pos = 0;
		while ((pos = out.find("\xE2\x80\xA8", pos)) != std::string::npos)
			out.replace(pos, 3, "\\u2028");
		pos = 0;
		while ((pos = out.find("\xE2\x80\xA9", pos)) != std::string::npos)
			out.replace(pos, 3, "\\u2029");
		out += "\"";
		return out;
	}

	std::string Json::Dump() const
	{
		switch (m_type)
		{
		case Null: return "null";
		case Bool: return m_bool ? "true" : "false";
		case Number:
		{
			std::ostringstream stream;
			stream.imbue(std::locale::classic());
			if (std::floor(m_number) == m_number && std::fabs(m_number) < 9007199254740992.0)
				stream << (long long)m_number;
			else
			{
				stream.precision(17);
				stream << m_number;
			}
			return stream.str();
		}
		case String: return Quote(m_string);
		case Array:
		{
			std::string out = "[";
			for (size_t i = 0; i < m_items.size(); ++i)
			{
				if (i)
					out += ",";
				out += m_items[i].Dump();
			}
			return out + "]";
		}
		case Object:
		{
			std::string out = "{";
			for (size_t i = 0; i < m_members.size(); ++i)
			{
				if (i)
					out += ",";
				out += Quote(m_members[i].first);
				out += ":";
				out += m_members[i].second.Dump();
			}
			return out + "}";
		}
		}
		return "null";
	}
}
