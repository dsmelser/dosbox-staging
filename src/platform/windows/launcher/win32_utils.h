// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LAUNCHER_WIN32_UTILS_H
#define DOSBOX_LAUNCHER_WIN32_UTILS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "misc/std_filesystem.h"

namespace Launcher {

std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

std_fs::path GetExecutableDir();

// Runs a console program without a window and returns its standard output,
// or nothing if it can't be started or exits with an error
std::optional<std::string> RunForOutput(const std_fs::path& program,
                                        const std::vector<std::string>& args);

bool StartProcess(const std_fs::path& program, const std::vector<std::string>& args,
                  const std_fs::path& working_dir, bool show_console);

std::string GetLastErrorMessage();

} // namespace Launcher

#endif
