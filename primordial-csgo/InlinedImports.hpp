#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "Image.hpp"

namespace inlined_imports
{
	struct InlinedImportInfo
	{
		std::uint32_t rva;
		std::string moduleName;
		std::string procName;
		bool isRelative;
	};

	extern const std::vector<InlinedImportInfo> g_inlinedImports;

	inline bool Fix()
	{
		// Cache module handles — avoid calling WaitForModule 6710 times for same DLLs
		std::unordered_map<std::string, HMODULE> moduleCache;

		for (const InlinedImportInfo& importInfo : g_inlinedImports)
		{
			HMODULE moduleBase;
			auto it = moduleCache.find(importInfo.moduleName);
			if (it != moduleCache.end())
			{
				moduleBase = it->second;
			}
			else
			{
				moduleBase = image::WaitForModule(importInfo.moduleName.c_str());
				if (moduleBase == nullptr)
				{
					image::Log(" FAIL: WaitForModule(\"%s\")\n", importInfo.moduleName.c_str());
					return false;
				}
				moduleCache[importInfo.moduleName] = moduleBase;
			}

			FARPROC procAddress = GetProcAddress(moduleBase, importInfo.procName.c_str());
			if (procAddress == nullptr)
			{
				image::Log(" FAIL: GetProcAddress(\"%s\", \"%s\") GLE=%lu\n",
					importInfo.moduleName.c_str(), importInfo.procName.c_str(), GetLastError());
				return false;
			}

			if (importInfo.isRelative)
				*reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(image::g_base) + importInfo.rva + 1) =
				reinterpret_cast<std::uintptr_t>(procAddress) - (reinterpret_cast<std::uintptr_t>(image::g_base) + importInfo.rva + 5);
			else
				*reinterpret_cast<FARPROC*>(reinterpret_cast<std::uintptr_t>(image::g_base) + importInfo.rva) = procAddress;
		}

		image::Log(" OK: %zu inlined imports resolved\n", g_inlinedImports.size());
		return true;
	}
}