// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LAUNCHER_LAUNCHER_DIALOG_H
#define DOSBOX_LAUNCHER_LAUNCHER_DIALOG_H

#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include "misc/std_filesystem.h"
#include "platform/windows/launcher/command_line.h"
#include "platform/windows/launcher/settings_schema.h"

namespace Launcher {

struct LauncherState {
	Item item               = {};
	std_fs::path dosbox_exe = {};
	SettingsSchema schema   = {};

	// Runs parallel to 'schema.settings'
	std::vector<std::optional<std::string>> overrides = {};

	LaunchOptions options = {};
};

// Lets the user choose the setting overrides and launch options; returns true
// if DOSBox should be started
bool ShowLauncherDialog(HINSTANCE instance, LauncherState& state);

} // namespace Launcher

#endif
