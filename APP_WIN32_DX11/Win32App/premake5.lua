project "Win32ThreadedApp"
	kind "WindowedApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "On" -- /MT
	optimize "Speed"
	floatingpoint "Fast"
	characterset ("MBCS")
	shadermodel ("4.0")

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"**.h",    -- Recurse into subdirectories
		"**.cpp",
		"**.inl",
		"**.hlsl",
		"**.ico",
		"**.png",
	}

	-- Additional include directories (take them all)
	includedirs
	{
		"%{prj.location}/**", -- Recurse subdirectories
		"%{prj.location}/assimp/include/assimp/*"
	}

	libdirs 
	{
		"%{prj.location}/assimp/bin/x64"
	}

	links
	{
		-- Link some libraries
		"d3d11", "dxgi", "d3dcompiler" , 
		"assimp-vc143-mt.lib"
	}

	-- ---- ---- HLSL files ---- ----
	include "premake5_shaders.lua"
	-- ---- ---- ---- ----  ---- ----

	filter "system:windows"
		systemversion "latest"
	-- spdlog stuff
	defines    { "SPDLOG_COMPILED_LIB", "_CRT_SECURE_NO_WARNINGS" }
	buildoptions { "/utf-8" }

	filter "configurations:Debug"
		runtime "Debug" -- /MTd
		defines { "IS_DEBUG=true" }
		symbols "on"

	filter "configurations:Release"
		runtime "Release" -- /MT
		defines { "NDEBUG" }
		defines { "IS_DEBUG=false" }
		optimize "On"

	filter "configurations:Dist"
		runtime "Release"
		defines { "NDEBUG" }
		optimize "On"
