-- Based on FGlob, this globber supports filtering into multiple configurations, and does so in the order specified
local function MGlob(args)
        -- Use the regular glob to fetch the file list.
        local files = Glob(args)
        local pats = {}
        local result = {}

        -- Construct a mapping from { Pattern = ..., Configs = ... }
        -- to { Pattern = { { Config = ... }, ... } } with new arrays per config that can be
        -- embedded in the source result.
        for _, fitem in ipairs(args.Filters) do
		local tabs = {}
		for _, citem in ipairs(fitem.Configs) do
                	local tab = { Config = assert(citem) }
			tabs[#tabs + 1] = tab
		end
                pats[assert(fitem.Pattern)] = tabs
                result[#result + 1] = tabs
        end

        -- Traverse all files and see if they match any configuration filters. If
        -- they do, stick them in matching list. Otherwise, just keep them in the
        -- main list. This has the effect of returning an array such as this:
        -- {
        --   { "foo.c"; Config = "abc-*-*" },
        --   { "bar.c"; Config = "*-*-def" },
        --   "baz.c", "qux.m"
        -- }
        for _, f in ipairs(files) do
                local filtered = false
                for _, fitem in ipairs(args.Filters) do
                        if f:match(fitem.Pattern) then
                                filtered = true
				for _, litem in ipairs(pats[fitem.Pattern]) do
					litem[#litem + 1] = f
				end
				break
                        end
                end
                if not filtered then
                        result[#result + 1] = f
                end
        end
        return result
end

local dependencies = { "contrib.fastlz" }
StaticLibrary
{
	Name = "streamer",
	SubConfig = { "ee"; Config = "ps2-*-*-*" },

	Sources = {
		{
			MGlob {
				Dir = "src",
				Extensions = { ".c" },
				Filters = {
					{ Pattern = "/win32/", Configs = { "win32-*-*-*" } },
					{ Pattern = "/ee/", Configs = { "ps2-*-*-*" } },
					{ Pattern = "/unix/", Configs = { "macosx-*-*-*", "linux-*-*-*" } },
					{ Pattern = "/iop/", Configs = {"ps2-*-*-iop*" } },
					{ Pattern = "/backend/", Configs = { "win32-*-*-*", "macosx-*-*-*", "linux-*-*-*" } }
				}
			}
		}
	},

	Propagate = {
		Libs = {
			{ "pthread"; Config = "linux-*-*-*" },
			{ "pthread"; Config = "macosx-*-*-*" },
		}
	},

	Env = {
		CPPPATH = {
			"contrib"
		},
	},

	Depends = {
		{ dependencies; Config = "linux-*-*-*" },
		{ dependencies; Config = "macosx-*-*-*" },
		{ dependencies; Config = "win32-*-*-*" },
		{ dependencies; Config = "win64-*-*-*" }
	}
}

-- PS2 IOP streaming IRX
SharedLibrary
{
	Name = "iopstream",
	SubConfig = "iop",
	Config = "ps2-*-*-*",

	Sources = {
		FGlob {
			Dir = "src/backend",
			Extensions = { ".c" },
			Filters = {
				{ Pattern = "/iop/"; Config = "ps2-*-*-*" }
			}
		},

		Glob {
			Dir = "contrib/fastlz", Extensions = { ".c" }
		}
	},

	Env = {
		CPPPATH = {
			"contrib"
		}
	},

	Depends = {
		"contrib.fastlz"
	}
}

-------------
-- Contrib --
-------------

Program
{
	Name = "filearchive",

	Sources = {
		Glob { Dir = "contrib/filearchive", Extensions = { ".c" } }
	},

	Env = {
		CPPPATH = "contrib"
	},

	Depends = {
		"contrib.fastlz"
	}
}

StaticLibrary
{
	Name = "contrib.fastlz",
	SubConfig = "iop",

	Sources = {
		Glob { Dir = "contrib/fastlz", Extensions = { ".c" } }
	},

	Env = {
		CCOPTS = {
			{ "/wd4244"; Config = "win32-*-*-*" },
			{ "/wd4244"; Config = "win64-*-*-*" }
		}
	},
}

-------------
-- Samples --
-------------

Program
{
	Name = "simple",

	Sources = {
		Glob { Dir = "samples/simple", Extensions = { ".c" } }
	},

	Env = {
		CPPPATH = "src"
	},

	Depends = { "streamer" }
}

Default "streamer"
Default "iopstream"
