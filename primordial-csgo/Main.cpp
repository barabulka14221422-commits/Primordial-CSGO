#include "ImportTable.hpp"
#include "InlinedImports.hpp"
#include "AddressTable.hpp"
#include "InlinedAddresses.hpp"
#include "Patches.hpp"
#include "Network.hpp"
#include "Hooks.hpp"

bool OnAttach(HMODULE hModule)
{
	// image::InitConsole();
	image::Log("Starting...\n");

	if (!image::Write(hModule)) { image::Log("FAIL: image::Write\n"); return false; }
	image::Log("OK: image::Write\n");

	if (!import_table::Fix()) { image::Log("FAIL: import_table::Fix\n"); return false; }
	image::Log("OK: import_table::Fix\n");

	if (!inlined_imports::Fix()) { image::Log("FAIL: inlined_imports::Fix\n"); return false; }
	image::Log("OK: inlined_imports::Fix\n");

	if (!address_table::Fix()) { image::Log("FAIL: address_table::Fix\n"); return false; }
	image::Log("OK: address_table::Fix\n");

	if (!inlined_addresses::Fix()) { image::Log("FAIL: inlined_addresses::Fix\n"); return false; }
	image::Log("OK: inlined_addresses::Fix\n");

	if (!patches::Write()) { image::Log("FAIL: patches::Write\n"); return false; }
	image::Log("OK: patches::Write\n");

	if (MH_Initialize() != MH_OK)
	{
		image::Log("FAIL: MH_Initialize\n");
		patches::Restore();
		return false;
	}
	image::Log("OK: MH_Initialize\n");

	if (!network::Fix())
	{
		image::Log("FAIL: network::Fix\n");
		MH_Uninitialize();
		patches::Restore();
		return false;
	}
	image::Log("OK: network::Fix\n");

	if (!image::CallEntryPoint())
	{
		image::Log("FAIL: image::CallEntryPoint\n");
		MH_Uninitialize();
		patches::Restore();
		return false;
	}
	image::Log("OK: image::CallEntryPoint\n");

	if (!hooks::Create())
	{
		image::Log("FAIL: hooks::Create\n");
		FatalExit(-1);
	}
	image::Log("OK: hooks::Create\n");

	image::Log("Initialized successfully!\n");
	return true;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpvReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
			{
				OnAttach(static_cast<HMODULE>(param));
				return 0;
			}, hModule, 0, nullptr);
		return TRUE;
	}

	return TRUE;
}