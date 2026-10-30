// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "platform/windows/launcher/command_line.h"
#include "platform/windows/launcher/settings_schema.h"

#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace Launcher {
namespace {

using Args = std::vector<std::string>;

// Command line building

TEST(LauncherCommandLine, QuotesArgumentsLikeCommandLineToArgvW)
{
	EXPECT_EQ(QuoteArg("simple"), "simple");
	EXPECT_EQ(QuoteArg(""), "\"\"");
	EXPECT_EQ(QuoteArg("two words"), "\"two words\"");
	EXPECT_EQ(QuoteArg("D:\\"), "D:\\");
	EXPECT_EQ(QuoteArg("C:\\My Games\\"), "\"C:\\My Games\\\\\"");
	EXPECT_EQ(QuoteArg("C:\\My Games\\GAME.EXE"), "\"C:\\My Games\\GAME.EXE\"");
	EXPECT_EQ(QuoteArg("say \"hi\""), "\"say \\\"hi\\\"\"");
	EXPECT_EQ(QuoteArg("a\\\"b"), "\"a\\\\\\\"b\"");
}

TEST(LauncherCommandLine, JoinsQuotedArguments)
{
	const Args args = {"C:\\Program Files\\DOSBox Staging\\dosbox.exe",
	                   "--set",
	                   "cpu cpu_cycles=5000",
	                   "--noautoexec"};

	EXPECT_EQ(JoinCommandLine(args),
	          "\"C:\\Program Files\\DOSBox Staging\\dosbox.exe\" --set "
	          "\"cpu cpu_cycles=5000\" --noautoexec");
}

TEST(LauncherCommandLine, RepairsExplorerDriveRootPaths)
{
	EXPECT_EQ(RepairExplorerPath("D:\""), "D:\\");
	EXPECT_EQ(RepairExplorerPath("C:\\Games"), "C:\\Games");
}

TEST(LauncherCommandLine, RecognisesDosFileNames)
{
	EXPECT_TRUE(IsDosFileName("GAME.EXE"));
	EXPECT_TRUE(IsDosFileName("game.exe"));
	EXPECT_TRUE(IsDosFileName("KEEN1.COM"));
	EXPECT_TRUE(IsDosFileName("GAME~1.BAT"));
	EXPECT_TRUE(IsDosFileName("SETUP"));

	EXPECT_FALSE(IsDosFileName("LONGNAME1.EXE"));
	EXPECT_FALSE(IsDosFileName("GAME.EXEC"));
	EXPECT_FALSE(IsDosFileName("MY GAME.EXE"));
	EXPECT_FALSE(IsDosFileName("A.B.C"));
	EXPECT_FALSE(IsDosFileName(".EXE"));
	EXPECT_FALSE(IsDosFileName(""));
}

class LauncherFolder : public ::testing::Test {
protected:
	void SetUp() override
	{
		std::error_code ec = {};
		std_fs::remove_all(folder, ec);
		ASSERT_TRUE(std_fs::create_directories(folder, ec));
	}

	void TearDown() override
	{
		std::error_code ec = {};
		std_fs::remove_all(folder, ec);
	}

	const std_fs::path folder = std_fs::temp_directory_path() /
	                            "dosbox_launcher_tests";
};

TEST_F(LauncherFolder, ClassifiesItems)
{
	EXPECT_EQ(ClassifyItem(folder).kind, ItemKind::Folder);
	EXPECT_EQ(ClassifyItem(folder / "GAME.EXE").kind, ItemKind::Program);
	EXPECT_EQ(ClassifyItem(folder / "keen1.Com").kind, ItemKind::Program);
	EXPECT_EQ(ClassifyItem(folder / "RUN.BAT").kind, ItemKind::Program);
	EXPECT_EQ(ClassifyItem(folder / "game.conf").kind, ItemKind::Config);
	EXPECT_EQ(ClassifyItem(folder / "README.TXT").kind, ItemKind::Unsupported);
}

TEST_F(LauncherFolder, MountsPlainFoldersByDefault)
{
	EXPECT_TRUE(ShouldMountFolderByDefault(folder));

	std::ofstream(folder / "dosbox.conf") << "[cpu]\n";
	EXPECT_FALSE(ShouldMountFolderByDefault(folder));

	std_fs::remove(folder / "dosbox.conf");
	std_fs::create_directory(folder / "drives");
	EXPECT_FALSE(ShouldMountFolderByDefault(folder));
}

TEST_F(LauncherFolder, BuildsFolderArgs)
{
	const auto item = ClassifyItem(folder);
	const auto dir  = PathToUtf8(folder);

	const auto machine_override = SettingOverride{"dosbox", "machine", "ega"};

	LaunchOptions options = {};
	EXPECT_EQ(BuildRunArgs(item, {machine_override}, options),
	          (Args{"--working-dir", dir, "--set", "dosbox machine=ega", dir}));

	options.mount_folder_as_c = false;
	options.show_console      = false;
	EXPECT_EQ(BuildRunArgs(item, {}, options),
	          (Args{"--working-dir", dir, "--noconsole"}));
}

const auto GameDir = std_fs::path("C:\\Games\\Keen");

TEST(LauncherCommandLine, BuildsProgramArgs)
{
	const Item item = {ItemKind::Program, GameDir / "KEEN1.EXE"};
	const auto dir  = PathToUtf8(GameDir);

	const auto machine = SettingOverride{"dosbox", "machine", "ega"};
	const auto cycles  = SettingOverride{"cpu", "cpu_cycles", "5000"};

	EXPECT_EQ(BuildConfigArgs(item), (Args{"--working-dir", dir}));

	EXPECT_EQ(BuildRunArgs(item, {machine, cycles}, {}),
	          (Args{"--working-dir",
	                dir,
	                "--set",
	                "dosbox machine=ega",
	                "--set",
	                "cpu cpu_cycles=5000",
	                "--noautoexec",
	                "-c",
	                "KEEN1.EXE",
	                dir}));
}

TEST(LauncherCommandLine, CallsBatchFiles)
{
	const Item item = {ItemKind::Program, GameDir / "run.bat"};
	const auto dir  = PathToUtf8(GameDir);

	LaunchOptions options      = {};
	options.exit_after_program = true;
	options.show_console       = false;

	EXPECT_EQ(BuildRunArgs(item, {}, options),
	          (Args{"--working-dir",
	                dir,
	                "--noautoexec",
	                "-c",
	                "CALL run.bat",
	                "--exit",
	                "--noconsole",
	                dir}));
}

TEST(LauncherCommandLine, OnlyMountsProgramsWithLongFileNames)
{
	const Item item = {ItemKind::Program, GameDir / "Commander Keen.exe"};
	const auto dir  = PathToUtf8(GameDir);

	LaunchOptions options      = {};
	options.exit_after_program = true;

	EXPECT_EQ(BuildRunArgs(item, {}, options),
	          (Args{"--working-dir", dir, "--noautoexec", dir}));
}

TEST(LauncherCommandLine, BuildsConfigArgs)
{
	const auto dir = PathToUtf8(GameDir);

	const Item item = {ItemKind::Config, GameDir / "keen.conf"};
	EXPECT_EQ(BuildRunArgs(item,
	                       {
	                               {"sdl", "fullscreen", "on"}
        },
	                       {}),
	          (Args{"--working-dir",
	                dir,
	                "--conf",
	                PathToUtf8(item.path),
	                "--set",
	                "sdl fullscreen=on"}));

	// DOSBox loads the local config by itself
	const Item local_config = {ItemKind::Config, GameDir / "DOSBox.conf"};
	EXPECT_EQ(BuildRunArgs(local_config, {}, {}), (Args{"--working-dir", dir}));
}

// Settings schema

constexpr auto SchemaJson = R"({
  "version": "0.84.0-alpha",
  "config_files": ["C:\\conf\\dosbox-staging.conf"],
  "sections": [
    { "name": "dosbox", "settings": [
      { "name": "machine", "type": "string", "value": "svga_s3",
        "default": "svga_s3", "values": ["svga_s3", "ega", "vgaonly"],
        "restricted": true, "help": "Machine type." },
      { "name": "memsize", "type": "int", "value": "16", "default": "16",
        "values": [], "restricted": false, "min": 1, "max": 63, "help": "" },
      { "name": "sbbase", "type": "hex", "value": "220", "default": "220",
        "values": [], "restricted": false, "help": "" },
      { "name": "scale", "type": "double", "value": "1.00", "default": "1.00",
        "values": [], "restricted": false, "help": "" } ] },
    { "name": "sdl", "settings": [
      { "name": "fullscreen", "type": "bool", "value": "off", "default": "off",
        "values": ["on", "off"], "restricted": true, "help": "" },
      { "type": "bool" } ] },
    { "name": "broken" }
  ]
})";

class LauncherSchema : public ::testing::Test {
protected:
	void SetUp() override
	{
		const auto parsed = ParseSettingsSchema(SchemaJson);
		ASSERT_TRUE(parsed);
		schema = *parsed;
		ASSERT_EQ(schema.settings.size(), 5u);
	}

	const Setting& machine() const
	{
		return schema.settings[0];
	}
	const Setting& memsize() const
	{
		return schema.settings[1];
	}
	const Setting& sbbase() const
	{
		return schema.settings[2];
	}
	const Setting& scale() const
	{
		return schema.settings[3];
	}
	const Setting& fullscreen() const
	{
		return schema.settings[4];
	}

	SettingsSchema schema = {};
};

TEST_F(LauncherSchema, ParsesSchema)
{
	EXPECT_EQ(schema.version, "0.84.0-alpha");
	EXPECT_EQ(schema.config_files, Args{"C:\\conf\\dosbox-staging.conf"});

	EXPECT_EQ(machine().section, "dosbox");
	EXPECT_EQ(machine().name, "machine");
	EXPECT_EQ(machine().type, SettingType::String);
	EXPECT_EQ(machine().value, "svga_s3");
	EXPECT_EQ(machine().values, (Args{"svga_s3", "ega", "vgaonly"}));
	EXPECT_TRUE(machine().restricted);
	EXPECT_EQ(machine().help, "Machine type.");

	EXPECT_EQ(memsize().type, SettingType::Int);
	EXPECT_EQ(memsize().min_value, 1);
	EXPECT_EQ(memsize().max_value, 63);
	EXPECT_FALSE(memsize().restricted);

	EXPECT_EQ(sbbase().type, SettingType::Hex);
	EXPECT_EQ(scale().type, SettingType::Double);

	EXPECT_EQ(fullscreen().section, "sdl");
	EXPECT_EQ(fullscreen().type, SettingType::Bool);
}

TEST(LauncherSchemaParsing, RejectsInvalidJson)
{
	EXPECT_FALSE(ParseSettingsSchema(""));
	EXPECT_FALSE(ParseSettingsSchema("{ not json"));
	EXPECT_FALSE(ParseSettingsSchema("[]"));
	EXPECT_FALSE(ParseSettingsSchema(R"({"version": "1"})"));
	EXPECT_FALSE(ParseSettingsSchema(R"({"sections": {}})"));
}

TEST(LauncherSchemaParsing, IgnoresMistypedFields)
{
	const auto schema = ParseSettingsSchema(R"({"version": 1, "sections": [
	        {"name": "cpu", "settings": [{"name": "core", "values": "auto",
	         "restricted": "yes", "min": "1"}]}]})");

	ASSERT_TRUE(schema);
	EXPECT_EQ(schema->version, "");
	ASSERT_EQ(schema->settings.size(), 1u);
	EXPECT_TRUE(schema->settings[0].values.empty());
	EXPECT_FALSE(schema->settings[0].restricted);
	EXPECT_FALSE(schema->settings[0].min_value);
}

TEST_F(LauncherSchema, NormalisesValues)
{
	EXPECT_EQ(NormaliseValue(fullscreen(), "TRUE"), "on");
	EXPECT_EQ(NormaliseValue(fullscreen(), "no"), "off");
	EXPECT_EQ(NormaliseValue(machine(), " EGA "), "ega");
	EXPECT_EQ(NormaliseValue(memsize(), " 42 "), "42");
}

TEST_F(LauncherSchema, ValidatesValues)
{
	EXPECT_TRUE(IsValidValue(memsize(), "42"));
	EXPECT_FALSE(IsValidValue(memsize(), "0"));
	EXPECT_FALSE(IsValidValue(memsize(), "64"));
	EXPECT_FALSE(IsValidValue(memsize(), "lots"));

	EXPECT_TRUE(IsValidValue(machine(), "ega"));
	EXPECT_FALSE(IsValidValue(machine(), "cga"));

	EXPECT_TRUE(IsValidValue(fullscreen(), "yes"));
	EXPECT_FALSE(IsValidValue(fullscreen(), "maybe"));

	EXPECT_TRUE(IsValidValue(sbbase(), "330"));
	EXPECT_TRUE(IsValidValue(sbbase(), "0x330"));
	EXPECT_FALSE(IsValidValue(sbbase(), "xyz"));

	EXPECT_TRUE(IsValidValue(scale(), "1.5"));
	EXPECT_FALSE(IsValidValue(scale(), "big"));
}

TEST_F(LauncherSchema, OnlyOverridesChangedValues)
{
	EXPECT_FALSE(MakeOverride(machine(), "svga_s3"));
	EXPECT_FALSE(MakeOverride(machine(), "SVGA_S3"));
	EXPECT_EQ(MakeOverride(machine(), "EGA"), "ega");

	EXPECT_FALSE(MakeOverride(fullscreen(), "false"));
	EXPECT_EQ(MakeOverride(fullscreen(), "true"), "on");
}

TEST_F(LauncherSchema, CollectsOverrides)
{
	std::vector<std::optional<std::string>> overrides(schema.settings.size());
	overrides[1] = "32";
	overrides[4] = "on";

	const auto collected = CollectOverrides(schema.settings, overrides);
	ASSERT_EQ(collected.size(), 2u);

	EXPECT_EQ(collected[0].section, "dosbox");
	EXPECT_EQ(collected[0].name, "memsize");
	EXPECT_EQ(collected[0].value, "32");

	EXPECT_EQ(collected[1].section, "sdl");
	EXPECT_EQ(collected[1].name, "fullscreen");
	EXPECT_EQ(collected[1].value, "on");
}

} // namespace
} // namespace Launcher
