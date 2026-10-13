#pragma once

#include <vector>
#include <string>

#include "Image.hpp"

namespace import_table
{
	struct ImportInfo
	{
		std::uint32_t rva;
		std::string moduleName;
		std::string procName;
	};

	extern const std::vector<ImportInfo> g_imports;

	inline bool Fix()
	{
		for (const ImportInfo& importInfo : g_imports)
		{
			HMODULE moduleBase = image::WaitForModule(importInfo.moduleName.c_str());
			if (moduleBase == nullptr)
			{
				image::Log(" FAIL: WaitForModule(\"%s\")\n", importInfo.moduleName.c_str());
				return false;
			}

			FARPROC procAddress = GetProcAddress(moduleBase, importInfo.procName.c_str());
			if (procAddress == nullptr)
			{
				image::Log(" FAIL: GetProcAddress(\"%s\", \"%s\") GLE=%lu\n",
					importInfo.moduleName.c_str(), importInfo.procName.c_str(), GetLastError());
				return false;
			}

			*reinterpret_cast<FARPROC*>(reinterpret_cast<std::uintptr_t>(image::g_base) + importInfo.rva) = procAddress;
		}

		image::Log(" OK: %zu imports resolved\n", g_imports.size());
		return true;
	}
}