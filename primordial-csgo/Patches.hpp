#pragma once

#include <string>
#include <vector>
#include <Windows.h>

#include "Image.hpp"

namespace patches
{
	struct PatchInfo
	{
		std::string moduleName;
		std::uint32_t rva;
		std::vector<std::uint8_t> patchBytes;
	};

	struct WrittenPatchInfo
	{
		void* address;
		std::vector<std::uint8_t> originalBytes;

		WrittenPatchInfo(void* address, std::size_t length) : address(address), originalBytes(length)
		{
			std::memcpy(originalBytes.data(), address, originalBytes.size());
		}
	};

	inline const std::vector<PatchInfo> g_patches = {
		{ "engine.dll", 0xDD185, { 0xFF, 0x00, 0x00, 0x00 } },
		{ "engine.dll", 0xDD3A3, { 0x00 } },
		{ "engine.dll", 0xDD1A5, { 0x46, 0xC7, 0x45, 0xB4, 0x00, 0x00 } },
		{ "engine.dll", 0xDD1A9, { 0x00, 0x00, 0x00, 0x00 } },
		{ "engine.dll", 0x2288EB, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } }
	};

	inline std::vector<WrittenPatchInfo> g_writtenPatches;

	inline void Restore()
	{
		for (const WrittenPatchInfo& patchInfo : g_writtenPatches)
		{
			DWORD oldProtect;
			if (VirtualProtect(patchInfo.address, patchInfo.originalBytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				std::memcpy(patchInfo.address, patchInfo.originalBytes.data(), patchInfo.originalBytes.size());
				VirtualProtect(patchInfo.address, patchInfo.originalBytes.size(), oldProtect, &oldProtect);
			}
		}

		g_writtenPatches.clear();
	}

	inline bool Write()
	{
		for (const PatchInfo& patchInfo : g_patches)
		{
			HMODULE moduleBase = image::WaitForModule(patchInfo.moduleName.c_str());
			if (moduleBase == nullptr)
			{
				image::Log(" FAIL: WaitForModule(\"%s\")\n", patchInfo.moduleName.c_str());
				Restore();
				return false;
			}

			void* patchAddress = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(moduleBase) + patchInfo.rva);

			DWORD oldProtect;
			if (!VirtualProtect(patchAddress, patchInfo.patchBytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				image::Log(" FAIL: VirtualProtect(\"%s\"+0x%X) GLE=%lu\n",
					patchInfo.moduleName.c_str(), patchInfo.rva, GetLastError());
				Restore();
				return false;
			}

			g_writtenPatches.emplace_back(patchAddress, patchInfo.patchBytes.size());
			std::memcpy(patchAddress, patchInfo.patchBytes.data(), patchInfo.patchBytes.size());
			VirtualProtect(patchAddress, patchInfo.patchBytes.size(), oldProtect, &oldProtect);
		}

		image::Log(" OK: %zu patches applied\n", g_patches.size());
		return true;
	}
}