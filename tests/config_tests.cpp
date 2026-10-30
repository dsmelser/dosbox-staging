// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "config/config.h"
#include "config/setup.h"

#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "json/json.h"

namespace {

struct StringCase {
	const char* input    = "";
	const char* expected = "";
};

class ConfigParsing : public ::testing::Test {
protected:
	void SetUp() override
	{
		previous_control = std::move(control);
		control          = std::make_unique<Config>(&command_line);
		section          = control->AddSection("parsing");
	}

	void TearDown() override
	{
		control = std::move(previous_control);
	}

	static constexpr auto WhenIdle = Property::Changeable::WhenIdle;

	CommandLine command_line{0, nullptr};
	ConfigPtr previous_control = {};
	SectionProp* section       = nullptr;
};

TEST_F(ConfigParsing, PreservesExistingStringQuoteAndWhitespaceHandling)
{
	const auto property = section->AddString("text", WhenIdle, "default");
	EXPECT_FALSE(property->PreservesQuotes());

	// clang-format off
	const StringCase cases[] = {
	        {"", ""},
	        {" \t\r\n\f ", ""},
	        {"value", "value"},
	        {" \tvalue\t ", "value"},
	        {"two  words", "two  words"},

	        {"\"value\"", "value"},
	        {"'value'", "value"},
	        {" \t\"  value \t\" \t", "value"},
	        {" \t'  value \t' \t", "value"},

	        {"\"\"", ""},
	        {"''", ""},
	        {"\" \t \"", ""},
	        {"' \t '", ""},

	        {"\"", "\""},
	        {"'", "'"},
	        {"\"value'", "\"value'"},
	        {"'value\"", "'value\""},
	        {"\"unclosed", "\"unclosed"},

	        {"\"'value'\"", "'value'"},
	        {"\"\"value\"\"", "\"value\""},
	        {"left=right", "left=right"},
	        {"\"FIRST=one\" \"SECOND=two\"", "FIRST=one\" \"SECOND=two"},
	};
	// clang-format on

	for (const auto& test : cases) {
		SCOPED_TRACE(test.input);

		ASSERT_TRUE(section->HandleInputLine(std::string(" \tTeXt \t= ") +
		                                     test.input));
		EXPECT_EQ(section->GetString("text"), test.expected);
	}
}

TEST_F(ConfigParsing, PreservesQuotesAndEnclosedWhitespaceOnlyWhenRequested)
{
	const auto property = section->AddString("arguments", WhenIdle, "");
	property->SetPreserveQuotes(true);
	section->AddString("text", WhenIdle, "");

	// clang-format off
	const StringCase cases[] = {
	        {"", ""},
	        {" \t ", ""},
	        {" \tvalue \t", "value"},

	        {" \t\"  value \t\" \t", "\"  value \t\""},
	        {" \t'  value \t' \t", "'  value \t'"},
	        {"\"\"", "\"\""},
	        {"''", "''"},

	        {"\"", "\""},
	        {"\"value'", "\"value'"},
	        {"\"unclosed", "\"unclosed"},

	        {R"( "FIRST=C:\My ROMs" "SECOND=another value" )",
	         R"("FIRST=C:\My ROMs" "SECOND=another value")"},
	};
	// clang-format on

	for (const auto& test : cases) {
		SCOPED_TRACE(test.input);

		ASSERT_TRUE(section->HandleInputLine(std::string("arguments = ") +
		                                     test.input));
		EXPECT_EQ(section->GetString("arguments"), test.expected);
	}

	ASSERT_TRUE(section->HandleInputLine("text = \"  value  \""));
	EXPECT_EQ(section->GetString("text"), "value");

	property->SetPreserveQuotes(false);
	ASSERT_TRUE(section->HandleInputLine("arguments = \"  value  \""));
	EXPECT_EQ(section->GetString("arguments"), "value");
}

TEST_F(ConfigParsing, PreservesQuotedScalarValues)
{
	section->AddInt("integer", WhenIdle, 0);
	section->AddBool("boolean", WhenIdle, false);
	section->AddHex("address", WhenIdle, 0);

	for (const auto* input : {"42", "\"42\"", "'42'", " \t\" 42 \t\" \t"}) {
		SCOPED_TRACE(input);

		ASSERT_TRUE(section->HandleInputLine(std::string("integer = ") +
		                                     input));
		EXPECT_EQ(section->GetInt("integer"), 42);
	}

	ASSERT_TRUE(section->HandleInputLine("boolean = \" true \t\""));
	EXPECT_TRUE(section->GetBool("boolean"));

	ASSERT_TRUE(section->HandleInputLine("boolean = ' false '"));
	EXPECT_FALSE(section->GetBool("boolean"));

	ASSERT_TRUE(section->HandleInputLine("address = ' 0x330 '"));
	EXPECT_EQ(section->GetHex("address"), Hex(0x330));
}

TEST_F(ConfigParsing, PreservesQuotedMultiValueSettings)
{
	const auto property = section->AddMultiVal("values", WhenIdle, " ");
	const auto values   = property->GetSection();
	values->AddString("mode", WhenIdle, "");
	values->AddInt("amount", WhenIdle, 0);

	ASSERT_TRUE(section->HandleInputLine("values = \"  mode 42  \""));
	EXPECT_EQ(section->GetPropertyValue("values"), "mode 42");
	EXPECT_EQ(values->GetString("mode"), "mode");
	EXPECT_EQ(values->GetInt("amount"), 42);
}

TEST_F(ConfigParsing, RejectsLinesWithoutAnAssignment)
{
	section->AddString("text", WhenIdle, "default");

	EXPECT_FALSE(section->HandleInputLine("text"));
	EXPECT_EQ(section->GetString("text"), "default");
}

nlohmann::json find_setting(const nlohmann::json& settings, const std::string& name)
{
	for (const auto& setting : settings) {
		if (setting.at("name") == name) {
			return setting;
		}
	}
	return {};
}

TEST_F(ConfigParsing, SettingsJsonDescribesSettings)
{
	const auto mode = section->AddString("json_test_mode", WhenIdle, "fast");
	mode->SetValues({"fast", "slow", "turbo"});
	mode->SetDeprecatedWithAlternateValue("turbo", "fast");

	const auto amount = section->AddInt("json_test_amount", WhenIdle, 10);
	amount->SetMinMax(1, 100);

	section->AddBool("json_test_enabled", WhenIdle, true);
	section->AddString("json_test_text", WhenIdle, "");
	section->AddPath("json_test_path", WhenIdle, "");
	section->AddString("json_test_retired", Property::Changeable::Deprecated, "");

	ASSERT_TRUE(section->HandleInputLine("json_test_amount = 42"));

	const auto json = nlohmann::json::parse(control->GetSettingsJson(),
	                                        nullptr,
	                                        false);
	ASSERT_FALSE(json.is_discarded());
	EXPECT_TRUE(json.at("version").is_string());
	EXPECT_TRUE(json.at("config_files").is_array());

	const auto& sections = json.at("sections");
	ASSERT_EQ(sections.size(), 1u);
	EXPECT_EQ(sections[0].at("name"), "parsing");

	const auto& settings = sections[0].at("settings");
	EXPECT_EQ(settings.size(), 5u);
	EXPECT_TRUE(find_setting(settings, "json_test_retired").is_null());

	const auto mode_json = find_setting(settings, "json_test_mode");
	EXPECT_EQ(mode_json.at("type"), "string");
	EXPECT_EQ(mode_json.at("value"), "fast");
	EXPECT_EQ(mode_json.at("default"), "fast");
	EXPECT_EQ(mode_json.at("values"), nlohmann::json({"fast", "slow"}));
	EXPECT_EQ(mode_json.at("restricted"), true);

	// Help texts come from the message system, which is stubbed out in the
	// tests
	EXPECT_TRUE(mode_json.at("help").is_string());

	const auto amount_json = find_setting(settings, "json_test_amount");
	EXPECT_EQ(amount_json.at("type"), "int");
	EXPECT_EQ(amount_json.at("value"), "42");
	EXPECT_EQ(amount_json.at("default"), "10");
	EXPECT_EQ(amount_json.at("min"), 1);
	EXPECT_EQ(amount_json.at("max"), 100);
	EXPECT_EQ(amount_json.at("restricted"), false);

	const auto enabled_json = find_setting(settings, "json_test_enabled");
	EXPECT_EQ(enabled_json.at("type"), "bool");
	EXPECT_EQ(enabled_json.at("value"), "on");
	EXPECT_EQ(enabled_json.at("values"), nlohmann::json({"on", "off"}));
	EXPECT_EQ(enabled_json.at("restricted"), true);

	const auto text_json = find_setting(settings, "json_test_text");
	EXPECT_EQ(text_json.at("type"), "string");
	EXPECT_TRUE(text_json.at("values").empty());
	EXPECT_EQ(text_json.at("restricted"), false);
	EXPECT_FALSE(text_json.contains("min"));

	EXPECT_EQ(find_setting(settings, "json_test_path").at("type"), "path");
}

} // namespace
