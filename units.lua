StaticLibrary
{
	Name = "streamer.frontend",
	SubConfig = "ee",

	Sources = {
		{
			FGlob {
				Dir = "src/frontend",
				Extensions = { ".c" },
				Filters = {
					{ Pattern = "/win32/", Extensions = { ".c" }, Config = "win32-*-*-*" },
					{ Pattern = "/ee/", Extensions = { ".c" }, Config = "ps2-*-*-*" }
				}
			}
		}
	},

	Depend = {
		{ "streamer.backend"; Config = "win32-*-*-*" },
		{ "streamer.backend"; Config = "win64-*-*-*" },
		{ "streamer.backend"; Config = "linux-*-*-*" },
		{ "streamer.backend"; Config = "macosx-*-*-*" },
		{ "streamer"; Config = "ps2-*-*-*" }
	}
}

StaticLibrary
{
	Name = "streamer.backend",

	Sources = {
		FGlob {
			Dir = "src/backend",
			Extensions = { ".c" },
			Filters = {
				{ Pattern = "/iop/"; Config = "ps2-*-*-*" }
			}
		}
	}
}

-- PS2 IOP streaming IRX
SharedLibrary
{
	Name = "streamer",
	SubConfig = "iop",
	Config = "ps2-*-*-*",

	Sources = {
		FGlob {
			Dir = "src/backend",
			Extensions = { ".c" },
			Filters = {
				{ Pattern = "/iop/"; Config = "ps2-*-*-*" }
			}
		}
	}
}

Default "streamer.frontend"
Default "streamer"
