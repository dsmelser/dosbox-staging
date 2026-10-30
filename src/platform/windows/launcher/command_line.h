// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LAUNCHER_COMMAND_LINE_H
#define DOSBOX_LAUNCHER_COMMAND_LINE_H

#include <string>
#include <string_view>
#include <vector>

#include "misc/std_filesystem.h"
#include "platform/windows/launcher/settings_schema.h"

namespace Launcher {

// The kinds of Windows Explorer items DOSBox can be started with
enum class ItemKind { Program, Folder, Config, Unsupported };

struct Item {
	ItemKind kind     = ItemKind::Unsupported;
	std_fs::path path = {};
};

struct LaunchOptions {
	// Program items only; otherwise DOSBox stays at the DOS prompt
	bool exit_after_program = false;

	// Folder items only
	bool mount_folder_as_c = true;

	bool show_console = true;
};

std_fs::path Utf8ToPath(std::string_view utf8);
std::string PathToUtf8(const std_fs::path& path);

// Windows Explorer expands a quoted "%V" placeholder for a drive root to
// "D:\" which reaches us as 'D:"'
std::string RepairExplorerPath(std::string path);

Item ClassifyItem(const std_fs::path& path);

// The folder DOSBox is started from: the folder item itself, or the folder
// containing a file item
std_fs::path GetWorkingDir(const Item& item);

// The DOS shell can only run programs with valid 8.3 file names
bool IsDosFileName(std::string_view file_name);

// Whether the program can be started with a '-c' command. Some valid DOS file
// names get altered on the way: DOSBox turns single quotes in '-c' commands
// into double quotes on Windows, and '%' starts a batch variable.
bool CanStartByName(std::string_view file_name);

// Folders set up for DOSBox (with a 'drives' folder or a local 'dosbox.conf')
// mount their drives themselves
bool ShouldMountFolderByDefault(const std_fs::path& folder);

// Makes DOSBox load the same configuration files as the actual run; also used
// to query the settings with '--list-settings-json'
std::vector<std::string> BuildConfigArgs(const Item& item);

std::vector<std::string> BuildRunArgs(const Item& item,
                                      const std::vector<SettingOverride>& overrides,
                                      const LaunchOptions& options);

// Quotes an argument according to the parsing rules of CommandLineToArgvW(),
// and if it contains characters cmd.exe would interpret
std::string QuoteArg(std::string_view arg);

std::string JoinCommandLine(const std::vector<std::string>& args);

// Quotes an argument for PowerShell if needed; single quotes also keep '$' and
// '`' from being interpreted
std::string QuotePowerShellArg(std::string_view arg);

// A PowerShell command running the program in the first argument
std::string JoinPowerShellCommandLine(const std::vector<std::string>& args);

} // namespace Launcher

#endif
