// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/launcher/launcher_dialog.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "platform/windows/launcher/resource.h"
#include "platform/windows/launcher/win32_utils.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace Launcher {

// Hand-picked settings for the Common tab; the ones missing from the schema
// (e.g., renamed in a later DOSBox version) are skipped
constexpr std::array<std::string_view, 19> CommonSettingNames = {
        "machine",
        "memsize",
        "ems",
        "xms",
        "core",
        "cputype",
        "cpu_cycles",
        "cpu_cycles_protected",
        "fullscreen",
        "output",
        "shader",
        "aspect",
        "integer_scaling",
        "sbtype",
        "oplmode",
        "gus",
        "mididevice",
        "joysticktype",
        "mouse_capture",
};

constexpr auto DialogTitle = L"Run with DOSBox Staging";

// Sent to the main dialog after an override has changed; WPARAM holds the
// page the change came from
constexpr UINT WmOverridesChanged = WM_APP + 1;

enum class Page : WPARAM { None, Common, AllSettings };

struct CommonControl {
	size_t setting_index = 0;
	HWND label           = nullptr;
	HWND combo           = nullptr;
};

struct FontDeleter {
	void operator()(const HFONT font) const
	{
		DeleteObject(font);
	}
};

using UniqueFont = std::unique_ptr<std::remove_pointer_t<HFONT>, FontDeleter>;

struct DialogContext {
	DialogContext(const HINSTANCE app_instance, LauncherState& launcher_state)
	        : instance(app_instance),
	          state(launcher_state)
	{}

	HINSTANCE instance = nullptr;
	LauncherState& state;

	HWND dialog      = nullptr;
	HWND tabs        = nullptr;
	HWND common_page = nullptr;
	HWND all_page    = nullptr;

	std::vector<CommonControl> common_controls = {};

	// The help texts are formatted for fixed-width fonts
	UniqueFont help_font                   = {};
	std::optional<size_t> selected_setting = {};

	// Set while controls get updated programmatically, so the resulting
	// change notifications are ignored
	bool is_updating = false;
};

static DialogContext* get_context(const HWND window)
{
	return reinterpret_cast<DialogContext*>(
	        GetWindowLongPtrW(window, GWLP_USERDATA));
}

static void set_context(const HWND window, const LPARAM context)
{
	SetWindowLongPtrW(window, GWLP_USERDATA, context);
}

static std::string get_text(const HWND window)
{
	const auto length = GetWindowTextLengthW(window);
	if (length <= 0) {
		return {};
	}
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');

	const auto num_copied = GetWindowTextW(window, text.data(), length + 1);
	text.resize(static_cast<size_t>(std::max(num_copied, 0)));

	return WideToUtf8(text);
}

static void set_text(const HWND window, const std::string_view text)
{
	SetWindowTextW(window, Utf8ToWide(text).c_str());
}

// Multi-line edit controls need CRLF line breaks
static std::string to_crlf(const std::string_view text)
{
	std::string result = {};
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
			result += '\r';
		}
		result += text[i];
	}
	return result;
}

static RECT dialog_units_to_pixels(const HWND dialog, const int x, const int y,
                                   const int width, const int height)
{
	RECT rect = {x, y, x + width, y + height};
	MapDialogRect(dialog, &rect);
	return rect;
}

static const Setting& get_setting(const DialogContext& context, const size_t index)
{
	return context.state.schema.settings[index];
}

static std::string get_effective_value(const DialogContext& context, const size_t index)
{
	const auto& override_value = context.state.overrides[index];
	return override_value ? *override_value : get_setting(context, index).value;
}

static std::optional<size_t> find_setting(const DialogContext& context,
                                          const std::string_view name)
{
	const auto& settings = context.state.schema.settings;

	const auto it = std::ranges::find(settings, name, &Setting::name);
	if (it == settings.end()) {
		return {};
	}
	return static_cast<size_t>(it - settings.begin());
}

static std::string describe_setting(const Setting& setting)
{
	return to_crlf("[" + setting.section + "] " + setting.name +
	               "\nCurrent value: " + setting.value +
	               "    Default value: " + setting.default_value + "\n\n" +
	               setting.help);
}

// The accepted values of a restricted setting, plus its current and default
// values in case DOSBox doesn't list them
static std::vector<std::string> get_choices(const Setting& setting)
{
	auto choices = setting.values;

	const auto add_if_missing = [&](const std::string& value) {
		const auto normalised = NormaliseValue(setting, value);
		if (std::ranges::find(choices, normalised) == choices.end()) {
			choices.insert(choices.begin(), normalised);
		}
	};
	add_if_missing(setting.value);
	if (!setting.default_value.empty()) {
		add_if_missing(setting.default_value);
	}
	return choices;
}

static std::string make_choice_label(const Setting& setting, const std::string& choice)
{
	const auto is_current = (choice == NormaliseValue(setting, setting.value));
	const auto is_default = (choice ==
	                         NormaliseValue(setting, setting.default_value));

	if (is_current && is_default) {
		return choice + "  (current, default)";
	}
	if (is_current) {
		return choice + "  (current)";
	}
	if (is_default) {
		return choice + "  (default)";
	}
	return choice;
}

// Restricted settings use drop-down lists, the others editable combo boxes
// whose list only holds suggestions
static void fill_value_combo(const HWND combo, const Setting& setting)
{
	ComboBox_ResetContent(combo);

	if (setting.restricted) {
		for (const auto& choice : get_choices(setting)) {
			ComboBox_AddString(
			        combo,
			        Utf8ToWide(make_choice_label(setting, choice)).c_str());
		}
	} else {
		// Offer the default value too, so it's easy to go back to
		auto suggestions = setting.values;
		if (!setting.default_value.empty() &&
		    std::ranges::find(suggestions, setting.default_value) ==
		            suggestions.end()) {
			suggestions.insert(suggestions.begin(), setting.default_value);
		}
		for (const auto& suggestion : suggestions) {
			ComboBox_AddString(combo, Utf8ToWide(suggestion).c_str());
		}
	}
}

static void show_value(const HWND combo, const Setting& setting,
                       const std::string& value)
{
	if (setting.restricted) {
		const auto choices = get_choices(setting);

		const auto it = std::ranges::find(choices,
		                                  NormaliseValue(setting, value));
		ComboBox_SetCurSel(combo,
		                   (it == choices.end())
		                           ? -1
		                           : static_cast<int>(it - choices.begin()));

	} else if (get_text(combo) != value) {
		set_text(combo, value);
	}
}

// Returns the value chosen or typed in a value combo box
static std::string read_value(const HWND combo, const Setting& setting,
                              const bool is_selection_change)
{
	const auto selected = ComboBox_GetCurSel(combo);

	if (setting.restricted) {
		const auto choices = get_choices(setting);
		if (selected >= 0 && static_cast<size_t>(selected) < choices.size()) {
			return choices[static_cast<size_t>(selected)];
		}
		return setting.value;
	}

	// The edit field only gets updated after the selection change
	// notification
	if (is_selection_change && selected >= 0) {
		const auto length = ComboBox_GetLBTextLen(combo, selected);
		if (length > 0) {
			std::wstring text(static_cast<size_t>(length) + 1, L'\0');
			ComboBox_GetLBText(combo, selected, text.data());
			text.resize(static_cast<size_t>(length));
			return WideToUtf8(text);
		}
	}
	return get_text(combo);
}

static void apply_value(DialogContext& context, const size_t index,
                        const std::string& value, const Page source)
{
	context.state.overrides[index] = MakeOverride(get_setting(context, index),
	                                              value);

	SendMessageW(context.dialog, WmOverridesChanged, static_cast<WPARAM>(source), 0);
}

static std::string build_command_line(const DialogContext& context)
{
	const auto& state = context.state;

	auto args = BuildRunArgs(state.item,
	                         CollectOverrides(state.schema.settings,
	                                          state.overrides),
	                         state.options);

	args.insert(args.begin(), PathToUtf8(state.dosbox_exe));

	return JoinCommandLine(args);
}

static void update_command_line(const DialogContext& context)
{
	set_text(GetDlgItem(context.dialog, IDC_COMMAND_LINE),
	         build_command_line(context));
}

// Common page

static HWND create_child(const DialogContext& context, const HWND parent,
                         const wchar_t* class_name, const std::string& text,
                         const DWORD style, const RECT& rect, const int id)
{
	const auto window = CreateWindowExW(0,
	                                    class_name,
	                                    Utf8ToWide(text).c_str(),
	                                    WS_CHILD | WS_VISIBLE | style,
	                                    rect.left,
	                                    rect.top,
	                                    rect.right - rect.left,
	                                    rect.bottom - rect.top,
	                                    parent,
	                                    reinterpret_cast<HMENU>(
	                                            static_cast<INT_PTR>(id)),
	                                    context.instance,
	                                    nullptr);

	SetWindowFont(window, GetWindowFont(parent), FALSE);
	return window;
}

static void create_common_controls(DialogContext& context)
{
	// Layout in dialog units
	constexpr auto NumColumns  = 2;
	constexpr auto Margin      = 4;
	constexpr auto RowHeight   = 14;
	constexpr auto LabelWidth  = 90;
	constexpr auto ComboWidth  = 110;
	constexpr auto ColumnWidth = LabelWidth + ComboWidth + 9;
	constexpr auto LabelHeight = 8;
	constexpr auto ListHeight  = 120;
	constexpr auto NoId        = -1;

	const auto page = context.common_page;

	for (const auto name : CommonSettingNames) {
		const auto index = find_setting(context, name);
		if (!index) {
			continue;
		}
		const auto& setting = get_setting(context, *index);

		const auto slot = static_cast<int>(context.common_controls.size());
		const auto x = Margin + (slot % NumColumns) * ColumnWidth;
		const auto y = Margin + (slot / NumColumns) * RowHeight;

		const auto label = create_child(
		        context,
		        page,
		        WC_STATICW,
		        setting.name,
		        SS_ENDELLIPSIS,
		        dialog_units_to_pixels(page, x, y + 2, LabelWidth - 4, LabelHeight),
		        NoId);

		const DWORD combo_style = WS_TABSTOP | WS_VSCROLL |
		                          (setting.restricted ? CBS_DROPDOWNLIST
		                                              : (CBS_DROPDOWN |
		                                                 CBS_AUTOHSCROLL));

		const auto combo = create_child(
		        context,
		        page,
		        WC_COMBOBOXW,
		        "",
		        combo_style,
		        dialog_units_to_pixels(page, x + LabelWidth, y, ComboWidth, ListHeight),
		        IDC_COMMON_FIRST + slot);

		fill_value_combo(combo, setting);
		show_value(combo, setting, get_effective_value(context, *index));

		context.common_controls.push_back({*index, label, combo});
	}
}

// Overridden settings get marked; values are only refreshed if the change
// came from elsewhere, so the caret of the edited combo box stays put
static void refresh_common_page(const DialogContext& context, const bool update_values)
{
	for (const auto& control : context.common_controls) {
		const auto index    = control.setting_index;
		const auto& setting = get_setting(context, index);

		const auto label = Utf8ToWide(setting.name);
		SetWindowTextW(control.label,
		               context.state.overrides[index]
		                       ? (L"\u25CF " + label).c_str()
		                       : label.c_str());
		if (update_values) {
			show_value(control.combo,
			           setting,
			           get_effective_value(context, index));
		}
	}
}

static INT_PTR CALLBACK common_page_proc(const HWND page, const UINT message,
                                         const WPARAM wparam, const LPARAM lparam)
{
	if (message == WM_INITDIALOG) {
		set_context(page, lparam);

		auto& context       = *get_context(page);
		context.common_page = page;

		EnableThemeDialogTexture(page, ETDT_ENABLETAB);
		create_common_controls(context);

		set_text(GetDlgItem(page, IDC_COMMON_HELP),
		         "Select a setting to see its description.");

		// Don't move the focus
		return FALSE;
	}

	const auto context = get_context(page);
	if (!context || message != WM_COMMAND) {
		return FALSE;
	}

	const auto slot = LOWORD(wparam) - IDC_COMMON_FIRST;
	if (slot < 0 || slot >= static_cast<int>(context->common_controls.size())) {
		return FALSE;
	}
	const auto& control = context->common_controls[static_cast<size_t>(slot)];
	const auto& setting = get_setting(*context, control.setting_index);

	switch (HIWORD(wparam)) {
	case CBN_SETFOCUS:
		set_text(GetDlgItem(page, IDC_COMMON_HELP),
		         describe_setting(setting));
		return TRUE;

	case CBN_SELCHANGE:
	case CBN_EDITCHANGE:
		if (!context->is_updating) {
			const auto is_selection_change = (HIWORD(wparam) ==
			                                  CBN_SELCHANGE);
			apply_value(*context,
			            control.setting_index,
			            read_value(control.combo, setting, is_selection_change),
			            Page::Common);
		}
		return TRUE;

	default: return FALSE;
	}
}

// All settings page

static bool matches_filter(const Setting& setting, const std::string& lower_filter)
{
	if (lower_filter.empty()) {
		return true;
	}
	const auto text = ToLowerAscii(setting.section + " " + setting.name +
	                               " " + setting.help);

	return text.find(lower_filter) != std::string::npos;
}

static std::wstring format_value_cell(const DialogContext& context, const size_t index)
{
	const auto value = Utf8ToWide(get_effective_value(context, index));

	// Mark the overridden settings
	return context.state.overrides[index] ? L"\u25CF " + value : value;
}

static void set_cell_text(const HWND list, const int item, const int column,
                          std::wstring text)
{
	ListView_SetItemText(list, item, column, text.data());
}

static size_t get_item_setting_index(const HWND list, const int item)
{
	LVITEMW list_item = {};
	list_item.mask    = LVIF_PARAM;
	list_item.iItem   = item;
	ListView_GetItem(list, &list_item);

	return static_cast<size_t>(list_item.lParam);
}

static void show_setting_editor(DialogContext& context,
                                const std::optional<size_t> index)
{
	const auto page       = context.all_page;
	const auto list_combo = GetDlgItem(page, IDC_VALUE_LIST);
	const auto edit_combo = GetDlgItem(page, IDC_VALUE_EDIT);

	context.selected_setting = index;

	if (!index) {
		ShowWindow(list_combo, SW_HIDE);
		ShowWindow(edit_combo, SW_HIDE);
		set_text(GetDlgItem(page, IDC_SETTING_NAME), "");
		set_text(GetDlgItem(page, IDC_SETTING_HELP),
		         "Select a setting to change its value.");
		EnableWindow(GetDlgItem(page, IDC_RESET_SETTING), FALSE);
		return;
	}

	const auto& setting = get_setting(context, *index);
	const auto combo    = setting.restricted ? list_combo : edit_combo;

	context.is_updating = true;
	fill_value_combo(combo, setting);
	show_value(combo, setting, get_effective_value(context, *index));
	context.is_updating = false;

	ShowWindow(list_combo, setting.restricted ? SW_SHOW : SW_HIDE);
	ShowWindow(edit_combo, setting.restricted ? SW_HIDE : SW_SHOW);

	set_text(GetDlgItem(page, IDC_SETTING_NAME), setting.name);
	set_text(GetDlgItem(page, IDC_SETTING_HELP), describe_setting(setting));

	EnableWindow(GetDlgItem(page, IDC_RESET_SETTING),
	             context.state.overrides[*index].has_value());
}

static void populate_settings_list(DialogContext& context)
{
	const auto page = context.all_page;
	const auto list = GetDlgItem(page, IDC_SETTINGS_LIST);
	const auto filter = ToLowerAscii(get_text(GetDlgItem(page, IDC_FILTER)));

	SetWindowRedraw(list, FALSE);
	ListView_DeleteAllItems(list);

	const auto& settings = context.state.schema.settings;

	auto num_items = 0;
	for (size_t i = 0; i < settings.size(); ++i) {
		if (!matches_filter(settings[i], filter)) {
			continue;
		}
		auto section = Utf8ToWide(settings[i].section);

		LVITEMW list_item = {};
		list_item.mask    = LVIF_TEXT | LVIF_PARAM;
		list_item.iItem   = num_items;
		list_item.pszText = section.data();
		list_item.lParam  = static_cast<LPARAM>(i);

		const auto item = ListView_InsertItem(list, &list_item);
		set_cell_text(list, item, 1, Utf8ToWide(settings[i].name));
		set_cell_text(list, item, 2, format_value_cell(context, i));

		++num_items;
	}
	SetWindowRedraw(list, TRUE);

	// Keep the selected setting selected if it's still listed
	auto selected_item = -1;
	if (context.selected_setting) {
		LVFINDINFOW find_info = {};
		find_info.flags       = LVFI_PARAM;
		find_info.lParam = static_cast<LPARAM>(*context.selected_setting);

		selected_item = ListView_FindItem(list, -1, &find_info);
	}
	if (selected_item >= 0) {
		constexpr UINT State = LVIS_SELECTED | LVIS_FOCUSED;
		ListView_SetItemState(list, selected_item, State, State);
		ListView_EnsureVisible(list, selected_item, FALSE);
	} else {
		show_setting_editor(context, {});
	}
}

static void refresh_settings_list_values(const DialogContext& context)
{
	const auto list = GetDlgItem(context.all_page, IDC_SETTINGS_LIST);

	const auto num_items = ListView_GetItemCount(list);
	for (auto item = 0; item < num_items; ++item) {
		const auto index = get_item_setting_index(list, item);
		set_cell_text(list, item, 2, format_value_cell(context, index));
	}
}

static void init_settings_list(const DialogContext& context)
{
	const auto page = context.all_page;
	const auto list = GetDlgItem(page, IDC_SETTINGS_LIST);

	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	// Column widths in dialog units
	const auto add_column = [&](const int index,
	                            const wchar_t* title,
	                            const int width) {
		std::wstring title_text = title;

		LVCOLUMNW column = {};
		column.mask      = LVCF_TEXT | LVCF_WIDTH;
		column.pszText   = title_text.data();
		column.cx = dialog_units_to_pixels(page, 0, 0, width, 0).right;

		ListView_InsertColumn(list, index, &column);
	};
	add_column(0, L"Section", 60);
	add_column(1, L"Setting", 110);
	add_column(2, L"Value", 220);

	Edit_SetCueBannerText(GetDlgItem(page, IDC_FILTER), L"Filter settings");
}

static INT_PTR CALLBACK all_settings_page_proc(const HWND page, const UINT message,
                                               const WPARAM wparam,
                                               const LPARAM lparam)
{
	if (message == WM_INITDIALOG) {
		set_context(page, lparam);

		auto& context    = *get_context(page);
		context.all_page = page;

		EnableThemeDialogTexture(page, ETDT_ENABLETAB);
		init_settings_list(context);
		populate_settings_list(context);

		// Don't move the focus
		return FALSE;
	}

	const auto context = get_context(page);
	if (!context) {
		return FALSE;
	}

	if (message == WM_NOTIFY) {
		const auto header = reinterpret_cast<const NMHDR*>(lparam);
		if (header->idFrom != IDC_SETTINGS_LIST ||
		    header->code != LVN_ITEMCHANGED) {
			return FALSE;
		}
		const auto change = reinterpret_cast<const NMLISTVIEW*>(lparam);

		const auto is_newly_selected = (change->uChanged & LVIF_STATE) &&
		                               (change->uNewState & LVIS_SELECTED) &&
		                               !(change->uOldState & LVIS_SELECTED);
		if (is_newly_selected) {
			show_setting_editor(*context,
			                    static_cast<size_t>(change->lParam));
		}
		return TRUE;
	}

	if (message != WM_COMMAND) {
		return FALSE;
	}

	const auto id   = LOWORD(wparam);
	const auto code = HIWORD(wparam);

	if (id == IDC_FILTER && code == EN_CHANGE) {
		populate_settings_list(*context);
		return TRUE;
	}

	const auto is_value_change = (id == IDC_VALUE_LIST || id == IDC_VALUE_EDIT) &&
	                             (code == CBN_SELCHANGE || code == CBN_EDITCHANGE);

	if (is_value_change && !context->is_updating && context->selected_setting) {
		const auto index = *context->selected_setting;
		apply_value(*context,
		            index,
		            read_value(GetDlgItem(page, id),
		                       get_setting(*context, index),
		                       code == CBN_SELCHANGE),
		            Page::AllSettings);
		return TRUE;
	}

	if (id == IDC_RESET_SETTING && code == BN_CLICKED &&
	    context->selected_setting) {
		context->state.overrides[*context->selected_setting].reset();
		SendMessageW(context->dialog,
		             WmOverridesChanged,
		             static_cast<WPARAM>(Page::None),
		             0);
		return TRUE;
	}
	return FALSE;
}

// Main dialog

static void refresh_views(DialogContext& context, const Page source)
{
	context.is_updating = true;
	refresh_common_page(context, source != Page::Common);
	refresh_settings_list_values(context);
	context.is_updating = false;

	// Refreshing the editor of the setting being edited would move the
	// caret
	if (source != Page::AllSettings) {
		show_setting_editor(context, context.selected_setting);
	} else if (context.selected_setting) {
		EnableWindow(GetDlgItem(context.all_page, IDC_RESET_SETTING),
		             context.state.overrides[*context.selected_setting].has_value());
	}

	update_command_line(context);
}

static void show_page(const DialogContext& context, const int tab)
{
	ShowWindow(context.common_page, (tab == 0) ? SW_SHOW : SW_HIDE);
	ShowWindow(context.all_page, (tab == 1) ? SW_SHOW : SW_HIDE);
}

static void place_page(const DialogContext& context, const HWND page)
{
	RECT rect = {};
	GetWindowRect(context.tabs, &rect);
	MapWindowPoints(nullptr, context.dialog, reinterpret_cast<POINT*>(&rect), 2);
	TabCtrl_AdjustRect(context.tabs, FALSE, &rect);

	SetWindowPos(page,
	             HWND_TOP,
	             rect.left,
	             rect.top,
	             rect.right - rect.left,
	             rect.bottom - rect.top,
	             SWP_NOACTIVATE);
}

static void add_tab(const HWND tabs, const int index, const wchar_t* title)
{
	std::wstring title_text = title;

	TCITEMW tab = {};
	tab.mask    = TCIF_TEXT;
	tab.pszText = title_text.data();

	TabCtrl_InsertItem(tabs, index, &tab);
}

static UniqueFont create_monospaced_font(const HWND window)
{
	constexpr std::wstring_view FaceName = L"Consolas";

	LOGFONTW font = {};
	GetObjectW(GetWindowFont(window), sizeof(font), &font);

	std::ranges::fill(font.lfFaceName, L'\0');
	std::ranges::copy(FaceName, font.lfFaceName);

	return UniqueFont(CreateFontIndirectW(&font));
}

// Lets a control fill the rest of the page's height
static void stretch_to_bottom(const HWND page, const int id)
{
	constexpr auto MarginDlu = 4;

	const auto control = GetDlgItem(page, id);

	RECT page_rect = {};
	GetClientRect(page, &page_rect);

	RECT rect = {};
	GetWindowRect(control, &rect);
	MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&rect), 2);

	const auto margin = dialog_units_to_pixels(page, 0, 0, 0, MarginDlu).bottom;

	SetWindowPos(control,
	             nullptr,
	             0,
	             0,
	             rect.right - rect.left,
	             page_rect.bottom - margin - rect.top,
	             SWP_NOMOVE | SWP_NOZORDER);
}

static void set_icons(const DialogContext& context)
{
	const auto load_icon = [&](const int size_metric) {
		return LoadImageW(context.instance,
		                  MAKEINTRESOURCEW(IDI_LAUNCHER),
		                  IMAGE_ICON,
		                  GetSystemMetrics(size_metric),
		                  GetSystemMetrics(size_metric),
		                  LR_DEFAULTCOLOR);
	};
	SendMessageW(context.dialog,
	             WM_SETICON,
	             ICON_BIG,
	             reinterpret_cast<LPARAM>(load_icon(SM_CXICON)));
	SendMessageW(context.dialog,
	             WM_SETICON,
	             ICON_SMALL,
	             reinterpret_cast<LPARAM>(load_icon(SM_CXSMICON)));
}

static void set_check(const HWND dialog, const int id, const bool is_checked)
{
	CheckDlgButton(dialog, id, is_checked ? BST_CHECKED : BST_UNCHECKED);
}

static bool is_checked(const HWND dialog, const int id)
{
	return IsDlgButtonChecked(dialog, id) == BST_CHECKED;
}

static void init_options(const DialogContext& context)
{
	const auto dialog   = context.dialog;
	const auto& item    = context.state.item;
	const auto& options = context.state.options;

	const auto is_program = (item.kind == ItemKind::Program);
	const auto is_folder  = (item.kind == ItemKind::Folder);

	ShowWindow(GetDlgItem(dialog, IDC_EXIT_AFTER), is_program ? SW_SHOW : SW_HIDE);
	ShowWindow(GetDlgItem(dialog, IDC_MOUNT_FOLDER),
	           is_folder ? SW_SHOW : SW_HIDE);

	set_check(dialog, IDC_EXIT_AFTER, options.exit_after_program);
	set_check(dialog, IDC_MOUNT_FOLDER, options.mount_folder_as_c);
	set_check(dialog, IDC_SHOW_CONSOLE, options.show_console);

	const auto file_name = PathToUtf8(item.path.filename());

	if (is_program && !CanStartByName(file_name)) {
		set_text(GetDlgItem(dialog, IDC_NOTE),
		         IsDosFileName(file_name)
		                 ? "DOSBox would alter this program's name when starting it; "
		                   "it will mount the folder as C: so you can start it yourself."
		                 : "DOS can't start programs with long file names; DOSBox will "
		                   "mount the folder as C: so you can start it by its short name.");
		EnableWindow(GetDlgItem(dialog, IDC_EXIT_AFTER), FALSE);
	}
}

static std::string get_item_name(const Item& item)
{
	const auto name = PathToUtf8(item.path.filename());
	return name.empty() ? PathToUtf8(item.path) : name;
}

static void init_main_dialog(DialogContext& context)
{
	const auto dialog = context.dialog;
	const auto& state = context.state;

	set_text(dialog, WideToUtf8(DialogTitle) + " - " + get_item_name(state.item));
	set_icons(context);

	set_text(GetDlgItem(dialog, IDC_ITEM_PATH), PathToUtf8(state.item.path));

	std::string config_files = {};
	for (const auto& file : state.schema.config_files) {
		config_files += (config_files.empty() ? "" : "; ") + file;
	}
	set_text(GetDlgItem(dialog, IDC_CONFIG_FILES),
	         config_files.empty() ? "(none)" : config_files);

	context.help_font = create_monospaced_font(dialog);

	add_tab(context.tabs, 0, L"Common");
	add_tab(context.tabs, 1, L"All settings");

	const auto context_param = reinterpret_cast<LPARAM>(&context);
	CreateDialogParamW(context.instance,
	                   MAKEINTRESOURCEW(IDD_COMMON_PAGE),
	                   dialog,
	                   common_page_proc,
	                   context_param);
	CreateDialogParamW(context.instance,
	                   MAKEINTRESOURCEW(IDD_ALL_SETTINGS_PAGE),
	                   dialog,
	                   all_settings_page_proc,
	                   context_param);

	place_page(context, context.common_page);
	place_page(context, context.all_page);
	show_page(context, 0);

	for (const auto [page, help_id] : {
	             std::pair{context.common_page,  IDC_COMMON_HELP},
	             std::pair{   context.all_page, IDC_SETTING_HELP}
        }) {
		SetWindowFont(GetDlgItem(page, help_id), context.help_font.get(), FALSE);
		stretch_to_bottom(page, help_id);
	}

	init_options(context);
	update_command_line(context);
}

static void copy_to_clipboard(const HWND owner, const std::string& text)
{
	const auto wide      = Utf8ToWide(text);
	const auto num_chars = wide.size() + 1;

	if (!OpenClipboard(owner)) {
		return;
	}
	EmptyClipboard();

	const auto memory = GlobalAlloc(GMEM_MOVEABLE, num_chars * sizeof(wchar_t));
	if (memory) {
		const auto data = static_cast<wchar_t*>(GlobalLock(memory));
		if (data) {
			std::copy_n(wide.c_str(), num_chars, data);
			GlobalUnlock(memory);
		}
		if (!data || !SetClipboardData(CF_UNICODETEXT, memory)) {
			GlobalFree(memory);
		}
	}
	CloseClipboard();
}

static bool validate_overrides(const DialogContext& context)
{
	const auto& settings = context.state.schema.settings;

	std::string invalid_settings = {};
	for (size_t i = 0; i < settings.size(); ++i) {
		const auto& override_value = context.state.overrides[i];
		if (override_value && !IsValidValue(settings[i], *override_value)) {
			invalid_settings += "\n" + settings[i].name + " = " +
			                  *override_value;
		}
	}
	if (invalid_settings.empty()) {
		return true;
	}

	const auto message = "These settings have invalid values:\n" + invalid_settings;
	MessageBoxW(context.dialog,
	            Utf8ToWide(message).c_str(),
	            DialogTitle,
	            MB_OK | MB_ICONWARNING);
	return false;
}

static bool handle_main_command(DialogContext& context, const int id)
{
	auto& options = context.state.options;

	switch (id) {
	case IDC_EXIT_AFTER:
		options.exit_after_program = is_checked(context.dialog, id);
		break;

	case IDC_MOUNT_FOLDER:
		options.mount_folder_as_c = is_checked(context.dialog, id);
		break;

	case IDC_SHOW_CONSOLE:
		options.show_console = is_checked(context.dialog, id);
		break;

	case IDC_COPY:
		copy_to_clipboard(context.dialog, build_command_line(context));
		return true;

	case IDC_RESET_ALL:
		std::ranges::fill(context.state.overrides, std::nullopt);
		refresh_views(context, Page::None);
		return true;

	case IDOK:
		if (validate_overrides(context)) {
			EndDialog(context.dialog, IDOK);
		}
		return true;

	case IDCANCEL: EndDialog(context.dialog, IDCANCEL); return true;

	default: return false;
	}

	update_command_line(context);
	return true;
}

static INT_PTR CALLBACK main_dialog_proc(const HWND dialog, const UINT message,
                                         const WPARAM wparam, const LPARAM lparam)
{
	if (message == WM_INITDIALOG) {
		set_context(dialog, lparam);

		auto& context  = *get_context(dialog);
		context.dialog = dialog;
		context.tabs   = GetDlgItem(dialog, IDC_TABS);

		init_main_dialog(context);
		return TRUE;
	}

	const auto context = get_context(dialog);
	if (!context) {
		return FALSE;
	}

	switch (message) {
	case WmOverridesChanged:
		refresh_views(*context, static_cast<Page>(wparam));
		return TRUE;

	case WM_NOTIFY: {
		const auto header = reinterpret_cast<const NMHDR*>(lparam);
		if (header->hwndFrom == context->tabs &&
		    header->code == TCN_SELCHANGE) {
			show_page(*context, TabCtrl_GetCurSel(context->tabs));
			return TRUE;
		}
		return FALSE;
	}

	case WM_COMMAND:
		return handle_main_command(*context, LOWORD(wparam)) ? TRUE : FALSE;

	default: return FALSE;
	}
}

bool ShowLauncherDialog(const HINSTANCE instance, LauncherState& state)
{
	DialogContext context(instance, state);

	const auto result = DialogBoxParamW(instance,
	                                    MAKEINTRESOURCEW(IDD_LAUNCHER),
	                                    nullptr,
	                                    main_dialog_proc,
	                                    reinterpret_cast<LPARAM>(&context));
	return result == IDOK;
}

} // namespace Launcher
