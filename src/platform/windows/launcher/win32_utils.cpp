// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/launcher/win32_utils.h"

#include <array>
#include <memory>

#include <windows.h>

#include "platform/windows/launcher/command_line.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace Launcher {

struct HandleCloser {
	void operator()(const HANDLE handle) const
	{
		if (handle && handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
		}
	}
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

std::wstring Utf8ToWide(const std::string_view utf8)
{
	if (utf8.empty()) {
		return {};
	}
	const auto utf8_length = static_cast<int>(utf8.size());

	const auto length = MultiByteToWideChar(
	        CP_UTF8, 0, utf8.data(), utf8_length, nullptr, 0);

	std::wstring wide(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8_length, wide.data(), length);

	return wide;
}

std::string WideToUtf8(const std::wstring_view wide)
{
	if (wide.empty()) {
		return {};
	}
	const auto wide_length = static_cast<int>(wide.size());

	const auto length = WideCharToMultiByte(
	        CP_UTF8, 0, wide.data(), wide_length, nullptr, 0, nullptr, nullptr);

	std::string utf8(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(
	        CP_UTF8, 0, wide.data(), wide_length, utf8.data(), length, nullptr, nullptr);

	return utf8;
}

std_fs::path GetExecutableDir()
{
	std::wstring buffer(MAX_PATH, L'\0');

	while (true) {
		const auto length = GetModuleFileNameW(
		        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0) {
			return {};
		}
		if (length < buffer.size()) {
			buffer.resize(length);
			return std_fs::path(buffer).parent_path();
		}
		// The path got truncated
		buffer.resize(buffer.size() * 2);
	}
}

static std::wstring make_command_line(const std_fs::path& program,
                                      const std::vector<std::string>& args)
{
	std::vector<std::string> all_args = {PathToUtf8(program)};
	all_args.insert(all_args.end(), args.begin(), args.end());

	return Utf8ToWide(JoinCommandLine(all_args));
}

std::optional<std::string> RunForOutput(const std_fs::path& program,
                                        const std::vector<std::string>& args)
{
	SECURITY_ATTRIBUTES inheritable = {};
	inheritable.nLength             = sizeof(inheritable);
	inheritable.bInheritHandle      = TRUE;

	HANDLE read_handle  = nullptr;
	HANDLE write_handle = nullptr;
	if (!CreatePipe(&read_handle, &write_handle, &inheritable, 0)) {
		return {};
	}
	const UniqueHandle read_pipe(read_handle);
	UniqueHandle write_pipe(write_handle);

	// Only the child gets the write end
	SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0);

	// Standard input and error go nowhere, so the child can't block on them
	UniqueHandle null_device(CreateFileW(L"NUL",
	                                     GENERIC_READ | GENERIC_WRITE,
	                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
	                                     &inheritable,
	                                     OPEN_EXISTING,
	                                     0,
	                                     nullptr));

	STARTUPINFOW startup_info = {};
	startup_info.cb           = sizeof(startup_info);
	startup_info.dwFlags      = STARTF_USESTDHANDLES;
	startup_info.hStdInput    = null_device.get();
	startup_info.hStdOutput   = write_pipe.get();
	startup_info.hStdError    = null_device.get();

	auto command_line = make_command_line(program, args);

	PROCESS_INFORMATION process_info = {};

	const auto started = CreateProcessW(program.c_str(),
	                                    command_line.data(),
	                                    nullptr,
	                                    nullptr,
	                                    TRUE,
	                                    CREATE_NO_WINDOW,
	                                    nullptr,
	                                    nullptr,
	                                    &startup_info,
	                                    &process_info);

	// Our copy of the write end must be closed, otherwise reading never
	// reaches the end of the output
	write_pipe.reset();
	null_device.reset();

	if (!started) {
		return {};
	}
	const UniqueHandle process(process_info.hProcess);
	const UniqueHandle thread(process_info.hThread);

	std::string output = {};

	std::array<char, 16384> buffer = {};
	DWORD num_bytes_read           = 0;
	while (ReadFile(read_pipe.get(),
	                buffer.data(),
	                static_cast<DWORD>(buffer.size()),
	                &num_bytes_read,
	                nullptr) &&
	       num_bytes_read > 0) {
		output.append(buffer.data(), num_bytes_read);
	}

	WaitForSingleObject(process.get(), INFINITE);

	DWORD exit_code = 1;
	if (!GetExitCodeProcess(process.get(), &exit_code) || exit_code != 0) {
		return {};
	}
	return output;
}

bool StartProcess(const std_fs::path& program, const std::vector<std::string>& args,
                  const std_fs::path& working_dir, const bool show_console)
{
	STARTUPINFOW startup_info = {};
	startup_info.cb           = sizeof(startup_info);

	auto command_line = make_command_line(program, args);

	// Console programs get a new console window by default
	const DWORD creation_flags = show_console ? 0 : CREATE_NO_WINDOW;

	PROCESS_INFORMATION process_info = {};

	const auto started = CreateProcessW(program.c_str(),
	                                    command_line.data(),
	                                    nullptr,
	                                    nullptr,
	                                    FALSE,
	                                    creation_flags,
	                                    nullptr,
	                                    working_dir.c_str(),
	                                    &startup_info,
	                                    &process_info);
	if (!started) {
		return false;
	}
	CloseHandle(process_info.hThread);
	CloseHandle(process_info.hProcess);
	return true;
}

std::string GetLastErrorMessage()
{
	const auto error = GetLastError();

	LPWSTR buffer = nullptr;

	const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
	                                           FORMAT_MESSAGE_FROM_SYSTEM |
	                                           FORMAT_MESSAGE_IGNORE_INSERTS,
	                                   nullptr,
	                                   error,
	                                   0,
	                                   reinterpret_cast<LPWSTR>(&buffer),
	                                   0,
	                                   nullptr);
	if (length == 0 || !buffer) {
		return "Error code " + std::to_string(error);
	}
	auto message = WideToUtf8(std::wstring_view(buffer, length));
	LocalFree(buffer);

	while (!message.empty() &&
	       (message.back() == '\n' || message.back() == '\r')) {
		message.pop_back();
	}
	return message;
}

} // namespace Launcher
