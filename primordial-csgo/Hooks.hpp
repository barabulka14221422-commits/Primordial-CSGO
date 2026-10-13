#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "mh/MinHook.h"

#include "Image.hpp"

namespace hooks
{
	struct HookInfo
	{
		std::string moduleName;
		std::uint32_t moduleRva;
		std::uint32_t detourRva;
		std::uint32_t originalRva;
	};

	inline const std::vector<HookInfo> g_hooks = {
		{ "client.dll", 0x1B4F30, 0x21A790, 0xCBF7E0 },
		{ "client.dll", 0x3420E0, 0x21B990, 0xCBF834 },
		{ "client.dll", 0x2F54E0, 0x48E650, 0xCEAE54 },
		{ "engine.dll", 0xDD290, 0x48EEE0, 0xCEAE5C },
		{ "client.dll", 0x26D5A0, 0x216160, 0xCBF63C },
		{ "engine.dll", 0x105140, 0x218970, 0xCBF738 },
		{ "client.dll", 0x305740, 0x48EF70, 0xCEAE58 },
		{ "client.dll", 0x6AA310, 0x4901A0, 0xCEAE9C },
		{ "client.dll", 0x518B40, 0x219840, 0xCBF780 },
		{ "client.dll", 0x3E8EA0, 0x21AB70, 0xCBF9FC },
		{ "client.dll", 0x3FE450, 0x216FF0, 0xCBF6A0 },
		{ "client.dll", 0x6A1E30, 0x490210, 0xCEAE98 },
		{ "engine.dll", 0x13E6A0, 0x21A2E0, 0xCA09F4 },
		{ "client.dll", 0x1ECAE0, 0x48D290, 0x0 },
		{ "client.dll", 0x44D000, 0x218770, 0xCBF71C },
		{ "client.dll", 0x44D0F0, 0x218790, 0xCBF718 },
		{ "materialsystem.dll", 0xCF90, 0x219FB0, 0xCBF7B8 },
		{ "client.dll", 0x3EACA0, 0x21AB80, 0xCBF7F0 },
		{ "client.dll", 0x226EF0, 0x218990, 0xCBF73C },
		{ "client.dll", 0x278BD0, 0x2171C0, 0xCBF6A8 },
		{ "client.dll", 0x6D5EF0, 0x218260, 0xCBF704 },
		{ "client.dll", 0x1E99B0, 0x48D2C0, 0xCEAE1C },
		{ "client.dll", 0x213390, 0x21BCA0, 0xCBF840 },
		{ "engine.dll", 0xB6EA0, 0x218B20, 0xCBF728 },
		{ "client.dll", 0x412370, 0x218C70, 0xCBF734 },
		{ "client.dll", 0x2707A0, 0x219FA0, 0xCBF7A8 },
		{ "engine.dll", 0x2FFA20, 0x4900F0, 0xCEAE8C },
		{ "client.dll", 0x261840, 0x216EF0, 0xCBF668 },
		{ "client.dll", 0x2618F0, 0x216F00, 0xCBF66C },
		{ "engine.dll", 0x137780, 0x218C90, 0xCBF72C },
		{ "client.dll", 0x43EEA0, 0x216350, 0xCBF64C },
		{ "client.dll", 0x1E8CE0, 0x48D440, 0xCEAE24 },
		{ "vguimatsurface.dll", 0x12260, 0x21BF60, 0xCBF888 },
		{ "engine.dll", 0x269D0, 0x219240, 0xCBF750 },
		{ "materialsystem.dll", 0x19FA0, 0x21A230, 0xCBF7BC },
		{ "client.dll", 0x276D70, 0x217250, 0xCBF6A4 },
		{ "client.dll", 0x3061F0, 0x216F30, 0xCA08FC },
		{ "client.dll", 0x436EE0, 0x21CB20, 0xCBF8B0 },
		{ "client.dll", 0x3DF170, 0x21ACB0, 0xCA09F0 },
		{ "client.dll", 0x1EB7B0, 0x48D6F0, 0xCEAE20 },
		{ "client.dll", 0x20DC70, 0x21AF60, 0xCBF7E8 },
		{ "client.dll", 0x534010, 0x219BA0, 0xCBF788 },
		{ "client.dll", 0x2BB170, 0x48E0B0, 0xCEAE48 },
		{ "engine.dll", 0x28F250, 0x21BDE0, 0xCBF874 },
		{ "engine.dll", 0x2866A0, 0x2179F0, 0xCBF6C4 },
		{ "client.dll", 0x437270, 0x21CC60, 0xCBF8B4 },
		{ "client.dll", 0x344290, 0x2175E0, 0xCBF6B4 },
		{ "client.dll", 0x4BDBB0, 0x219C90, 0xCBF77C },
		{ "engine.dll", 0x256910, 0x217F20, 0xCBF6C8 },
		{ "client.dll", 0x1A9BD0, 0x4902A0, 0xCEAEA4 },
		{ "client.dll", 0x6993E0, 0x21BCD0, 0xCBF838 },
		{ "client.dll", 0x3E8480, 0x21AFD0, 0xCBF7E4 },
		{ "client.dll", 0x440CF0, 0x2163B0, 0xCBF658 },
		{ "client.dll", 0x443410, 0x2163E0, 0xCBF654 },
		{ "client.dll", 0x440AE0, 0x216410, 0xCBF65C },
		{ "client.dll", 0x1ADF00, 0x48D750, 0xCEAE28 },
		{ "client.dll", 0x213590, 0x21BD30, 0xCBF83C },
		{ "engine.dll", 0x1DCB60, 0x218CC0, 0xCBF730 },
		{ "client.dll", 0x94FA30, 0x21B550, 0xCBF828 },
		{ "client.dll", 0x69A0D0, 0x21BDC0, 0xCBF864 },
		{ "engine.dll", 0x3A020, 0x2193B0, 0xCBF754 },
		{ "client.dll", 0x7B22D0, 0x2187B0, 0xCBF714 },
		{ "client.dll", 0x3F6880, 0x2187D0, 0xCBF710 },
		{ "client.dll", 0x1D7F90, 0x21B170, 0xCBF7F4 },
		{ "client.dll", 0x43E9E0, 0x216440, 0xCBF650 },
		{ "client.dll", 0x3FA450, 0x217480, 0xCBF698 },
		{ "client.dll", 0x20BBD0, 0x21B4C0, 0xCBF7EC },
		{ "client.dll", 0x20F500, 0x21B4D0, 0xCBF7F8 },
		{ "client.dll", 0x3FAEA0, 0x2175C0, 0xCBF69C },
		{ "inputsystem.dll", 0x3AC0, 0x219F60, 0xCBF798 },
		{ "vgui2.dll", 0x19520, 0x21BF80, 0xCBF88C }
	};

	inline bool Create()
	{
		// Cache module handles — avoids redundant WaitForModule per hook entry
		std::unordered_map<std::string, HMODULE> moduleCache;

		for (const HookInfo& hookInfo : g_hooks)
		{
			HMODULE moduleBase;
			auto it = moduleCache.find(hookInfo.moduleName);
			if (it != moduleCache.end())
			{
				moduleBase = it->second;
			}
			else
			{
				moduleBase = image::WaitForModule(hookInfo.moduleName.c_str());
				if (moduleBase == nullptr)
				{
					image::Log(" FAIL: WaitForModule(\"%s\")\n", hookInfo.moduleName.c_str());
					return false;
				}
				moduleCache[hookInfo.moduleName] = moduleBase;
			}

			void* functionAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(moduleBase) + hookInfo.moduleRva);
			void* detourAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(image::g_base) + hookInfo.detourRva);
			void** originalAddress = hookInfo.originalRva != 0
				? reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(image::g_base) + hookInfo.originalRva)
				: nullptr;

			if (MH_CreateHook(functionAddress, detourAddress, originalAddress) != MH_OK)
			{
				image::Log(" FAIL: MH_CreateHook(\"%s\"+0x%X)\n",
					hookInfo.moduleName.c_str(), hookInfo.moduleRva);
				return false;
			}
		}

		// Enable ALL hooks in one shot — single icache flush instead of 70+
		if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
		{
			image::Log(" FAIL: MH_EnableHook(ALL)\n");
			return false;
		}

		image::Log(" OK: %zu hooks installed\n", g_hooks.size());
		return true;
	}
}