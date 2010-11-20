module(..., package.seeall)

local native = require "tundra.native"
local os = require "os"

function apply(env, options)
	options = options or {}
	local target_arch = options.TargetArch or "ee"
	local sdk_path = options.CustomPath or env:get_external_env_var("PS2SDK")

	if target_arch == "ee" or target_arch == "iop" then
		env:append("LIBPATH", sdk_path .. "$(SEP)" .. target_arch .. "$(SEP)lib")
		env:append("CPPPATH", sdk_path .. "$(SEP)" .. target_arch .. "$(SEP)include")
		env:replace("_GCC_BINPREFIX", target_arch .. "-")
		if target_arch == "ee" then
			env:replace("PROGSUFFIX", ".elf")
		else
			env:replace("SHLIBPREFIX", "")
			env:replace("SHLIBSUFFIX", ".irx")
			env:replace("SHLIBCOM", "$(PROGCOM)")
			env:append("CCOPTS", "-miop")
			env:append("PROGOPTS", "-miop")
			env:append("PROGOPTS", "-nostdlib")
		end
	else
		error("Unsupported architecture '" .. target_arch .. "'")
	end 

	env:append("LIBPATH", sdk_path .. "$(SEP)common$(SEP)lib")
	env:append("CPPPATH", sdk_path .. "$(SEP)common$(SEP)include")
end
