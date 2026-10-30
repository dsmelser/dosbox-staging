// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/launcher/command_line.h"

#include <algorithm>
#include <optional>
#include <system_error>

#include "utils/checks.h"

CHECK_NARROWING();

namespace Launcher {

std_fs::path Utf8ToPath(const std::string_view utf8)
{
	return std_fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string PathToUtf8(const std_fs::path& path)
{
	const auto utf8 = path.u8string();
	return std::string(utf8.begin(), utf8.end());
}

static std::string get_lower_extension(const std_fs::path& path)
{
	return ToLowerAscii(PathToUtf8(path.extension()));
}

std::string RepairExplorerPath(std::string path)
{
	if (path.ends_with('"')) {
		path.back() = '\\';
	}
	return path;
}

Item ClassifyItem(const std_fs::path& path)
{
	std::error_code ec = {};
	if (std_fs::is_directory(path, ec)) {
		return {ItemKind::Folder, path};
	}

	const auto extension = get_lower_extension(path);
	if (extension == ".exe" || extension == ".com" || extension == ".bat") {
		return {ItemKind::Program, path};
	}
	if (extension == ".conf") {
		return {ItemKind::Config, path};
	}
	return {ItemKind::Unsupported, path};
}

std_fs::path GetWorkingDir(const Item& item)
{
	return (item.kind == ItemKind::Folder) ? item.path
	                                       : item.path.parent_path();
}

static bool is_dos_file_name_char(const char c)
{
	constexpr std::string_view SpecialChars = "!#$%&'()-@^_`{}~";

	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') ||
	       SpecialChars.find(c) != std::string_view::npos;
}

bool IsDosFileName(const std::string_view file_name)
{
	constexpr auto MaxBaseNameLength  = 8;
	constexpr auto MaxExtensionLength = 3;

	const auto dot_pos   = file_name.find('.');
	const auto base_name = file_name.substr(0, dot_pos);
	const auto extension = (dot_pos == std::string_view::npos)
	                             ? std::string_view()
	                             : file_name.substr(dot_pos + 1);

	return !base_name.empty() && base_name.size() <= MaxBaseNameLength &&
	       extension.size() <= MaxExtensionLength &&
	       std::ranges::all_of(base_name, is_dos_file_name_char) &&
	       std::ranges::all_of(extension, is_dos_file_name_char);
}

bool ShouldMountFolderByDefault(const std_fs::path& folder)
{
	std::error_code ec = {};
	return !std_fs::exists(folder / "drives", ec) &&
	       !std_fs::exists(folder / "dosbox.conf", ec);
}

// DOSBox loads 'dosbox.conf' from the working dir by itself
static bool is_local_config(const Item& item)
{
	return item.kind == ItemKind::Config &&
	       ToLowerAscii(PathToUtf8(item.path.filename())) == "dosbox.conf";
}

std::vector<std::string> BuildConfigArgs(const Item& item)
{
	std::vector<std::string> args = {"--working-dir",
	                                 PathToUtf8(GetWorkingDir(item))};

	if (item.kind == ItemKind::Config && !is_local_config(item)) {
		args.emplace_back("--conf");
		args.emplace_back(PathToUtf8(item.path));
	}
	return args;
}

std::vector<std::string> BuildRunArgs(const Item& item,
                                      const std::vector<SettingOverride>& overrides,
                                      const LaunchOptions& options)
{
	auto args = BuildConfigArgs(item);

	for (const auto& setting : overrides) {
		args.emplace_back("--set");
		args.emplace_back(setting.section + " " + setting.name + "=" +
		                  setting.value);
	}

	// The PATH argument must come last
	std::optional<std::string> path_arg = {};

	switch (item.kind) {
	case ItemKind::Program: {
		// Skipping the [autoexec] sections makes the program the first
		// DOS command; the folder given as PATH gets mounted as C:
		// before the '-c' commands run.
		args.emplace_back("--noautoexec");

		const auto file_name = PathToUtf8(item.path.filename());
		if (IsDosFileName(file_name)) {
			const auto is_batch_file = (get_lower_extension(item.path) ==
			                            ".bat");
			args.emplace_back("-c");
			args.emplace_back(is_batch_file ? "CALL " + file_name
			                                : file_name);

			if (options.exit_after_program) {
				args.emplace_back("--exit");
			}
		}
		path_arg = PathToUtf8(GetWorkingDir(item));
		break;
	}
	case ItemKind::Folder:
		if (options.mount_folder_as_c) {
			path_arg = PathToUtf8(item.path);
		}
		break;

	case ItemKind::Config:
	case ItemKind::Unsupported: break;
	}

	if (!options.show_console) {
		args.emplace_back("--noconsole");
	}
	if (path_arg) {
		args.push_back(*path_arg);
	}
	return args;
}

std::string QuoteArg(const std::string_view arg)
{
	const auto needs_quotes = arg.empty() || arg.find_first_of(" \t\n\v\"") !=
	                                                 std::string_view::npos;
	if (!needs_quotes) {
		return std::string(arg);
	}

	std::string quoted = "\"";

	size_t num_backslashes = 0;
	for (const auto c : arg) {
		if (c == '\\') {
			++num_backslashes;
			continue;
		}
		// Backslashes only need escaping in front of a quote
		if (c == '"') {
			quoted.append(num_backslashes * 2 + 1, '\\');
		} else {
			quoted.append(num_backslashes, '\\');
		}
		quoted += c;
		num_backslashes = 0;
	}

	// Trailing backslashes would escape the closing quote
	quoted.append(num_backslashes * 2, '\\');
	quoted += '"';

	return quoted;
}

std::string JoinCommandLine(const std::vector<std::string>& args)
{
	std::string command_line = {};
	for (const auto& arg : args) {
		if (&arg != &args.front()) {
			command_line += ' ';
		}
		command_line += QuoteArg(arg);
	}
	return command_line;
}

} // namespace Launcher
