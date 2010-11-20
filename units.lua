-- Based on FGlob, this globber supports filtering into multiple configurations
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
                for filter, list in pairs(pats) do
                        if f:match(filter) then
                                filtered = true
				for _, litem in ipairs(list) do
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
	Name = "streamer.frontend",
	SubConfig = "ee",

	Sources = {
		{
			MGlob {
				Dir = "src/frontend",
				Extensions = { ".c" },
				Filters = {
					{ Pattern = "/win32/", Extensions = { ".c" }, Configs = { "win32-*-*-*" } },
					{ Pattern = "/ee/", Extensions = { ".c" }, Configs = { "ps2-*-*-*" } },
					{ Pattern = "/unix/", Extensions = { ".c" }, Configs = { "macosx-*-*-*", "linux-*-*-*" } }
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
