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

StaticLibrary
{
	Name = "streamer",
	SubConfig = { "ee"; Config = "ps2-*-*-*" },

	Sources = {
		{
			MGlob {
				Dir = "src/streamer",
				Extensions = { ".c" },
				Filters = {
					{ Pattern = "/win32/", Configs = { "win*-*-*-*" } },
					{ Pattern = "/ee/", Configs = { "ps2-*-*-*" } },
					{ Pattern = "/unix/", Configs = { "macosx-*-*-*", "linux-*-*-*" } },
					{ Pattern = "/iop/", Configs = {"ps2-*-*-iop*" } },
					{ Pattern = "/backend/", Configs = { "win*-*-*-*", "macosx-*-*-*", "linux-*-*-*" } }
				}
			},
			MGlob {
				Dir = "src/contrib/fastlz",
				Extensions = { ".c" },
				Filters = {
					{ Pattern = "/", Configs = { "win*-*-*-*", "macosx-*-*-*", "linux-*-*-*" } } 
				}
			}
		}
	},

	Propagate = {
		Libs = {
			{ "pthread"; Config = "linux-*-*-*" },
			{ "pthread"; Config = "macosx-*-*-*" },
		},
	},

	Env = {
		CPPPATH = {
			"src/contrib"
		},
	},
}

-- PS2 IOP streaming IRX
SharedLibrary
{
	Name = "iopstrmr",
	SubConfig = "iop",
	Config = "ps2-*-*-*",

	Sources = {
		FGlob {
			Dir = "src/streamer/backend",
			Extensions = { ".c" },
			Filters = {
				{ Pattern = "/iop/"; Config = "ps2-*-*-*" }
			}
		},

		Glob {
			Dir = "src/contrib/fastlz", Extensions = { ".c" }
		}
	},

	Env = {
		CPPPATH = {
			"src/contrib"
		}
	},
}

StaticLibrary
{
	Name = "contrib.sha1",

	Sources = {
		Glob { Dir = "src/contrib/sha1", Extensions = { ".c" } }
	}
}

-------------
-- Samples --
-------------

Program
{
	Name = "sample.simple",

	Sources = {
		Glob { Dir = "src/samples/simple", Extensions = { ".c" } }
	},

	Env = {
		CPPPATH = "src"
	},

	Depends = { "streamer", "contrib.fastlz" }
}

Program
{
	Name = "sample.hash",

	Sources = {
		Glob { Dir = "src/samples/hash", Extensions = { ".c" } }
	},

	Env = {
		CPPPATH = { "src", "src/contrib" }
	},

	Depends = { "streamer", "contrib.fastlz", "contrib.sha1" }
}

Default "streamer"
Default "iopstrmr"
