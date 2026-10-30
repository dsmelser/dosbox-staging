// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LAUNCHER_SETTINGS_SCHEMA_H
#define DOSBOX_LAUNCHER_SETTINGS_SCHEMA_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Launcher {

enum class SettingType { Bool, Int, Hex, Double, String, Path, MultiVal };

struct Setting {
	std::string section       = {};
	std::string name          = {};
	SettingType type          = SettingType::String;
	std::string value         = {};
	std::string default_value = {};

	// The accepted values if 'restricted' is set, otherwise just suggestions
	std::vector<std::string> values = {};
	bool restricted                 = false;

	std::optional<int> min_value = {};
	std::optional<int> max_value = {};

	std::string help = {};
};

struct SettingsSchema {
	std::string version                   = {};
	std::vector<std::string> config_files = {};

	// All settings of all sections, in the order DOSBox lists them
	std::vector<Setting> settings = {};
};

struct SettingOverride {
	std::string section = {};
	std::string name    = {};
	std::string value   = {};
};

std::string ToLowerAscii(std::string_view text);

// Parses the output of 'dosbox --list-settings-json'
std::optional<SettingsSchema> ParseSettingsSchema(std::string_view json_text);

// Returns the canonical form of a value (e.g., 'on' for 'true' in bool
// settings, or the listed spelling of restricted values)
std::string NormaliseValue(const Setting& setting, std::string_view value);

bool IsValidValue(const Setting& setting, std::string_view value);

// Returns the override to apply if the value differs from the setting's
// current value
std::optional<std::string> MakeOverride(const Setting& setting,
                                        std::string_view value);

// Pairs the settings with their overrides; 'overrides' runs parallel to
// 'settings'
std::vector<SettingOverride> CollectOverrides(
        const std::vector<Setting>& settings,
        const std::vector<std::optional<std::string>>& overrides);

} // namespace Launcher

#endif
