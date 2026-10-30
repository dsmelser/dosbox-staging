// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/launcher/settings_schema.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <type_traits>

#include "json/json.h"

#include "utils/checks.h"

CHECK_NARROWING();

namespace Launcher {

using Json = nlohmann::json;

std::string ToLowerAscii(const std::string_view text)
{
	std::string result(text);
	for (auto& c : result) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return result;
}

static std::string_view trim(const std::string_view text)
{
	constexpr std::string_view Whitespace = " \t\r\n";

	const auto first = text.find_first_not_of(Whitespace);
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = text.find_last_not_of(Whitespace);
	return text.substr(first, last - first + 1);
}

// The json accessors below never throw; missing or mistyped fields yield
// empty values

static std::string get_string(const Json& object, const char* key)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_string()) {
		return {};
	}
	return it->get<std::string>();
}

static std::optional<int> get_int(const Json& object, const char* key)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number_integer()) {
		return {};
	}
	return it->get<int>();
}

static std::vector<std::string> get_strings(const Json& object, const char* key)
{
	std::vector<std::string> strings = {};

	const auto it = object.find(key);
	if (it == object.end() || !it->is_array()) {
		return strings;
	}
	for (const auto& element : *it) {
		if (element.is_string()) {
			strings.push_back(element.get<std::string>());
		}
	}
	return strings;
}

static SettingType parse_setting_type(const std::string_view type)
{
	if (type == "bool") {
		return SettingType::Bool;
	}
	if (type == "int") {
		return SettingType::Int;
	}
	if (type == "hex") {
		return SettingType::Hex;
	}
	if (type == "double") {
		return SettingType::Double;
	}
	if (type == "path") {
		return SettingType::Path;
	}
	if (type == "multival") {
		return SettingType::MultiVal;
	}
	return SettingType::String;
}

static Setting parse_setting(const std::string& section_name, const Json& json)
{
	Setting setting = {};

	setting.section       = section_name;
	setting.name          = get_string(json, "name");
	setting.type          = parse_setting_type(get_string(json, "type"));
	setting.value         = get_string(json, "value");
	setting.default_value = get_string(json, "default");
	setting.values        = get_strings(json, "values");
	setting.min_value     = get_int(json, "min");
	setting.max_value     = get_int(json, "max");
	setting.help          = get_string(json, "help");

	const auto restricted = json.find("restricted");
	setting.restricted    = (restricted != json.end() &&
                              restricted->is_boolean() && restricted->get<bool>());

	return setting;
}

std::optional<SettingsSchema> ParseSettingsSchema(const std::string_view json_text)
{
	constexpr auto AllowExceptions = false;

	const auto json = Json::parse(json_text, nullptr, AllowExceptions);
	if (json.is_discarded() || !json.is_object()) {
		return {};
	}

	const auto sections = json.find("sections");
	if (sections == json.end() || !sections->is_array()) {
		return {};
	}

	SettingsSchema schema = {};
	schema.version        = get_string(json, "version");
	schema.config_files   = get_strings(json, "config_files");

	for (const auto& section : *sections) {
		const auto section_name = get_string(section, "name");

		const auto settings = section.find("settings");
		if (section_name.empty() || settings == section.end() ||
		    !settings->is_array()) {
			continue;
		}
		for (const auto& json_setting : *settings) {
			auto setting = parse_setting(section_name, json_setting);
			if (!setting.name.empty()) {
				schema.settings.push_back(std::move(setting));
			}
		}
	}
	return schema;
}

static std::optional<bool> parse_bool(const std::string_view value)
{
	const auto lower = ToLowerAscii(value);

	for (const auto truthy : {"on", "true", "yes", "1", "enabled"}) {
		if (lower == truthy) {
			return true;
		}
	}
	for (const auto falsy : {"off", "false", "no", "0", "disabled"}) {
		if (lower == falsy) {
			return false;
		}
	}
	return {};
}

std::string NormaliseValue(const Setting& setting, const std::string_view value)
{
	const auto trimmed = trim(value);

	if (setting.type == SettingType::Bool) {
		if (const auto bool_value = parse_bool(trimmed); bool_value) {
			return *bool_value ? "on" : "off";
		}
	}

	const auto lower = ToLowerAscii(trimmed);
	for (const auto& listed_value : setting.values) {
		if (ToLowerAscii(listed_value) == lower) {
			return listed_value;
		}
	}
	return std::string(trimmed);
}

template <typename T>
static std::optional<T> parse_number(const std::string_view text, const int base = 10)
{
	if (text.empty()) {
		return {};
	}

	T number = {};

	const auto end    = text.data() + text.size();
	const auto result = [&] {
		if constexpr (std::is_floating_point_v<T>) {
			return std::from_chars(text.data(), end, number);
		} else {
			return std::from_chars(text.data(), end, number, base);
		}
	}();

	if (result.ec != std::errc() || result.ptr != end) {
		return {};
	}
	return number;
}

bool IsValidValue(const Setting& setting, const std::string_view value)
{
	const auto normalised = NormaliseValue(setting, value);

	if (setting.restricted) {
		return std::ranges::find(setting.values, normalised) !=
		       setting.values.end();
	}

	switch (setting.type) {
	case SettingType::Bool: return parse_bool(normalised).has_value();

	case SettingType::Int: {
		const auto number = parse_number<int>(normalised);
		if (!number) {
			return false;
		}
		return (!setting.min_value || *number >= *setting.min_value) &&
		       (!setting.max_value || *number <= *setting.max_value);
	}

	case SettingType::Hex: {
		constexpr auto HexBase = 16;

		auto digits = std::string_view(normalised);
		if (digits.starts_with("0x") || digits.starts_with("0X")) {
			digits.remove_prefix(2);
		}
		return parse_number<int64_t>(digits, HexBase).has_value();
	}

	case SettingType::Double:
		return parse_number<double>(normalised).has_value();

	case SettingType::String:
	case SettingType::Path:
	case SettingType::MultiVal: return true;
	}
	return true;
}

std::optional<std::string> MakeOverride(const Setting& setting,
                                        const std::string_view value)
{
	auto normalised = NormaliseValue(setting, value);

	// Normalising unifies the spelling of booleans and listed values, while
	// case matters in free-form values like window titles
	const auto current = NormaliseValue(setting, setting.value);
	if (normalised == current) {
		return {};
	}
	return normalised;
}

std::vector<SettingOverride> CollectOverrides(
        const std::vector<Setting>& settings,
        const std::vector<std::optional<std::string>>& overrides)
{
	assert(settings.size() == overrides.size());

	std::vector<SettingOverride> collected = {};
	for (size_t i = 0; i < settings.size() && i < overrides.size(); ++i) {
		if (overrides[i]) {
			collected.push_back({settings[i].section,
			                     settings[i].name,
			                     *overrides[i]});
		}
	}
	return collected;
}

} // namespace Launcher
