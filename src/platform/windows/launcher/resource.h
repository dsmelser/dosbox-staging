// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LAUNCHER_RESOURCE_H
#define DOSBOX_LAUNCHER_RESOURCE_H

// Resource IDs shared by the resource script and the code; the resource
// compiler only understands preprocessor defines.

#define IDI_LAUNCHER 100

#define IDD_LAUNCHER          101
#define IDD_COMMON_PAGE       102
#define IDD_ALL_SETTINGS_PAGE 103

// Main dialog
#define IDC_ITEM_PATH    1001
#define IDC_CONFIG_FILES 1002
#define IDC_TABS         1003
#define IDC_NOTE         1004
#define IDC_EXIT_AFTER   1005
#define IDC_MOUNT_FOLDER 1006
#define IDC_SHOW_CONSOLE 1007
#define IDC_COMMAND_LINE 1008
#define IDC_COPY         1009
#define IDC_RESET_ALL    1010

// Common page; its setting controls are created at runtime with IDs
// starting at IDC_COMMON_FIRST
#define IDC_COMMON_HELP  1101
#define IDC_COMMON_FIRST 1200

// All settings page
#define IDC_FILTER        1301
#define IDC_SETTINGS_LIST 1302
#define IDC_SETTING_NAME  1303
#define IDC_VALUE_LIST    1304
#define IDC_VALUE_EDIT    1305
#define IDC_RESET_SETTING 1306
#define IDC_SETTING_HELP  1307

#endif
