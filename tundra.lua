local common = {
	Env = {
		CPPDEFS = {
			{ "STREAMER_DEBUG"; Config = "*-*-debug-*" },
			{ "STREAMER_RELEASE"; Config = "*-*-release-*" },
			{ "STREAMER_FINAL"; Config = "*-*-final-*" },

			{ "STREAMER_PS2_SCE"; Config = "*-*-*-sce" },
		},

		CCOPTS_FINAL = "$(CCOPTS_RELEASE)"
	}
}

Build {
	Units = "units.lua",
	SyntaxExtensions = { "tundra.syntax.glob" },
	ScriptDirs = { "scripts" },

	Configs = {
		Config { Name = "win32-msvc", Inherit = common, Env = { CPPDEFS = { "STREAMER_WIN32" } }, Tools = { { "msvc-winsdk"; TargetArch = "x86" } } },
		Config { Name = "win64-msvc", Inherit = common, Env = { CPPDEFS = { "STREAMER_WIN32" } }, Tools = { { "msvc-winsdk"; TargetArch = "x64" } } },
		Config { Name = "macosx-clang", Inherit = common, Env = { CPPDEFS = { "STREAMER_UNIX" } }, Tools = { "clang-osx" } },
		Config { Name = "linux-gcc", Inherit = common, Env = { CPPDEFS = { "STREAMER_UNIX" } }, Tools = { "gcc" } },
		Config { Name = "ps2-gcc", SubConfigs = { iop = "ps2-iopgcc", ee = "ps2-eegcc" }, DefaultSubConfig = "ee" },

		Config { Name = "ps2-iopgcc", Virtual = true, Inherit = common, Tools = { "gcc", { "tundra.tools.ps2sdk"; TargetArch = "iop" } }, Env = { CPPDEFS = { "STREAMER_PS2", "_IOP" } } },
		Config { Name = "ps2-eegcc", Virtual = true, Inherit = common, Tools = { "gcc", { "tundra.tools.ps2sdk"; TargetArch = "ee" } }, Env = { CPPDEFS = { "STREAMER_PS2" } } }
	},

	Variants = { "debug", "release", "final" },

	SubVariants = { "default", "sce" }
}
