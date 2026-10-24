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

#ifndef MARKETCANOPY_JSON_H
#define MARKETCANOPY_JSON_H

#include <string>
#include <utility>
#include <vector>

namespace NSMarketCanopy
{
	// RFC 8259 JSON, parsed strictly: no comments, no trailing commas, no
	// duplicate keys, nesting at most 64 deep. Objects keep their key order so
	// a value written by Dump() is byte-stable. Numbers are kept as doubles,
	// which is enough for every integer the contract carries (sizes, seconds).
	class Json
	{
	public:
		enum Type { Null, Bool, Number, String, Array, Object };

		Json() : m_type(Null), m_bool(false), m_number(0) {}
		static Json MakeBool(bool value);
		static Json MakeNumber(double value);
		static Json MakeString(const std::string& value);
		static Json MakeArray();
		static Json MakeObject();

		// false when the text is not exactly one JSON value (whitespace allowed around it)
		static bool Parse(const std::string& text, Json& out);

		Type GetType() const { return m_type; }
		bool IsNull() const { return m_type == Null; }
		bool IsBool() const { return m_type == Bool; }
		bool IsNumber() const { return m_type == Number; }
		bool IsString() const { return m_type == String; }
		bool IsArray() const { return m_type == Array; }
		bool IsObject() const { return m_type == Object; }

		bool AsBool() const { return m_bool; }
		double AsNumber() const { return m_number; }
		const std::string& AsString() const { return m_string; }

		// Arrays
		size_t Size() const { return m_type == Array ? m_items.size() : (m_type == Object ? m_members.size() : 0); }
		const Json& At(size_t index) const;
		void Push(const Json& value);

		// Objects. A missing key reads as a Json of type Null; Has() tells them apart.
		bool Has(const std::string& key) const;
		const Json& Get(const std::string& key) const;
		void Set(const std::string& key, const Json& value);
		const std::vector<std::pair<std::string, Json> >& Members() const { return m_members; }

		std::string Dump() const;

		static std::string Quote(const std::string& value);

	private:
		Type m_type;
		bool m_bool;
		double m_number;
		std::string m_string;
		std::vector<Json> m_items;
		std::vector<std::pair<std::string, Json> > m_members;
	};
}

#endif // MARKETCANOPY_JSON_H
