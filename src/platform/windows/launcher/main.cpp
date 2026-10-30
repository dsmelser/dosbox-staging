// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

// Windows Explorer context menu helper: lets the user pick setting overrides
// for a single DOSBox run, then starts DOSBox with the right-clicked program,
// folder or config file.

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>

#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include "misc/std_filesystem.h"
#include "platform/windows/launcher/command_line.h"
#include "platform/windows/launcher/launcher_dialog.h"
#include "platform/windows/launcher/settings_schema.h"
#include "platform/windows/launcher/win32_utils.h"
#include "utils/checks.h"

CHECK_NARROWING();

static void show_error(const std::string& message)
{
	MessageBoxW(nullptr,
	            Launcher::Utf8ToWide(message).c_str(),
	            L"Run with DOSBox Staging",
	            MB_OK | MB_ICONERROR);
}

static std::optional<std::string> get_item_arg()
{
	auto num_args   = 0;
	const auto args = CommandLineToArgvW(GetCommandLineW(), &num_args);
	if (!args) {
		return {};
	}

	std::optional<std::string> item_arg = {};
	if (num_args >= 2) {
		item_arg = Launcher::WideToUtf8(args[1]);
	}
	LocalFree(args);

	return item_arg;
}

static std::optional<Launcher::SettingsSchema> query_settings(
        const std_fs::path& dosbox_exe, const Launcher::Item& item)
{
	auto args = Launcher::BuildConfigArgs(item);
	args.emplace_back("--list-settings-json");

	const auto output = Launcher::RunForOutput(dosbox_exe, args);
	if (!output) {
		return {};
	}
	return Launcher::ParseSettingsSchema(*output);
}

// The setting is on by default, should a DOSBox version not list it
static bool is_automount_enabled(const Launcher::SettingsSchema& schema)
{
	const auto it = std::ranges::find(schema.settings,
	                                  "automount",
	                                  &Launcher::Setting::name);

	return it == schema.settings.end() || it->value == "on";
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	const auto item_arg = get_item_arg();
	if (!item_arg) {
		show_error(
		        "Right-click a DOS program, a folder or a .conf file in "
		        "Windows Explorer and choose 'Run with DOSBox Staging "
		        "(choose settings)...' to use this program.");
		return 1;
	}

	std::error_code ec = {};

	const auto item_path = std_fs::absolute(
	        Launcher::Utf8ToPath(Launcher::RepairExplorerPath(*item_arg)), ec);

	const auto item = Launcher::ClassifyItem(item_path);
	if (ec || !std_fs::exists(item_path, ec) ||
	    item.kind == Launcher::ItemKind::Unsupported) {
		show_error("DOSBox Staging can't start '" + *item_arg +
		           "'.\n\nOnly DOS programs (.exe, .com, .bat), folders "
		           "and .conf files are supported.");
		return 1;
	}

	const auto dosbox_exe = Launcher::GetExecutableDir() / "dosbox.exe";
	if (!std_fs::exists(dosbox_exe, ec)) {
		show_error("Can't find '" + Launcher::PathToUtf8(dosbox_exe) + "'.");
		return 1;
	}

	const auto schema = query_settings(dosbox_exe, item);
	if (!schema) {
		show_error("Can't read the settings from '" +
		           Launcher::PathToUtf8(dosbox_exe) +
		           "'.\n\nTry running it with the '--list-settings-json' "
		           "option from a command prompt to see what's wrong.");
		return 1;
	}

	INITCOMMONCONTROLSEX common_controls = {};
	common_controls.dwSize               = sizeof(common_controls);
	common_controls.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES |
	                        ICC_STANDARD_CLASSES;
	InitCommonControlsEx(&common_controls);

	Launcher::LauncherState state = {};

	state.item       = item;
	state.dosbox_exe = dosbox_exe;
	state.schema     = *schema;
	state.overrides.resize(schema->settings.size());

	if (item.kind == Launcher::ItemKind::Folder) {
		state.options.mount_folder_as_c = Launcher::ShouldMountFolderByDefault(
		        item.path, is_automount_enabled(*schema));
	}

	if (!Launcher::ShowLauncherDialog(instance, state)) {
		return 0;
	}

	const auto args = Launcher::BuildRunArgs(
	        item,
	        Launcher::CollectOverrides(state.schema.settings, state.overrides),
	        state.options);

	if (!Launcher::StartProcess(dosbox_exe,
	                            args,
	                            Launcher::GetWorkingDir(item),
	                            state.options.show_console)) {
		const auto error_message = Launcher::GetLastErrorMessage();
		show_error("Can't start '" + Launcher::PathToUtf8(dosbox_exe) +
		           "': " + error_message);
		return 1;
	}
	return 0;
}
