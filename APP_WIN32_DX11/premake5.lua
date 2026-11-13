include "./vendor/premake/premake_customization/solution_items.lua"

workspace "AppSolution_Win32_DX11"
	architecture "x86_64"
	startproject "Win32App"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	solution_items
	{
		".editorconfig"
	}

	flags
	{
		"MultiProcessorCompile"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "vendor/premake"
group ""

group "Core"
include "Win32App"
group ""

