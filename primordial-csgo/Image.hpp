#pragma once

#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <Windows.h>

#include "resource.h"

namespace image
{
	inline void* g_base;
	inline std::size_t g_size;
	inline HANDLE g_consoleHandle = INVALID_HANDLE_VALUE;

	inline void InitConsole()
	{
		AllocConsole();
		SetConsoleTitleA("primordial");

		g_consoleHandle = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, 0, nullptr);

		HANDLE hNul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, 0, nullptr);

		SetStdHandle(STD_OUTPUT_HANDLE, hNul);
		SetStdHandle(STD_ERROR_HANDLE, hNul);

		FILE* nul;
		freopen_s(&nul, "NUL", "w", stdout);
		freopen_s(&nul, "NUL", "w", stderr);

		SetConsoleActiveScreenBuffer(g_consoleHandle);
	}

	inline void Log(const char* fmt, ...)
	{
		if (g_consoleHandle == INVALID_HANDLE_VALUE)
			return;

		char buf[1024];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);

		DWORD written;
		WriteConsoleA(g_consoleHandle, buf, static_cast<DWORD>(strlen(buf)), &written, nullptr);
	}

	inline HMODULE WaitForModule(const char* moduleName, DWORD timeoutMs = 30000, DWORD settlementMs = 5000)
	{
		HMODULE moduleBase = GetModuleHandleA(moduleName);
		if (moduleBase != nullptr)
			return moduleBase;

		moduleBase = LoadLibraryA(moduleName);
		if (moduleBase != nullptr)
		{
			image::Log("[*] Loaded via LoadLibraryA: \"%s\"\n", moduleName);
			return moduleBase;
		}

		image::Log("[/] Waiting for: \"%s\"\n", moduleName);
		DWORD elapsed = 0;
		while (elapsed < timeoutMs)
		{
			Sleep(100);
			elapsed += 100;

			moduleBase = GetModuleHandleA(moduleName);
			if (moduleBase != nullptr)
			{
				image::Log("OK: \"%s\" appeared after %lums\n", moduleName, elapsed);
				if (settlementMs > 0)
				{
					image::Log("Waiting for full game initialization\n");
					Sleep(settlementMs);
				}
				return moduleBase;
			}
		}

		image::Log("TIMEOUT: \"%s\" not loaded after %lums\n", moduleName, timeoutMs);
		return nullptr;
	}

	inline bool Write(HMODULE hModule)
	{
		HRSRC resourceInfo = FindResourceA(hModule, reinterpret_cast<LPCSTR>(IDR_PRIMORDIAL), "PRIMORDIAL");
		if (resourceInfo == nullptr) { Log("FAIL: FindResourceA\n"); return false; }

		g_size = SizeofResource(hModule, resourceInfo);
		if (g_size == 0) { Log("FAIL: SizeofResource\n"); return false; }
		Log("Resource size: 0x%zX\n", g_size);

		HGLOBAL resourceData = LoadResource(hModule, resourceInfo);
		if (resourceData == nullptr) { Log("FAIL: LoadResource\n"); return false; }

		void* cheatBinary = LockResource(resourceData);
		if (cheatBinary == nullptr) { Log("FAIL: LockResource\n"); return false; }

		g_base = reinterpret_cast<LPVOID>(0x40000000);
		Log("Target base: 0x40000000\n");

		MEMORY_BASIC_INFORMATION memoryInfo;
		VirtualQuery(g_base, &memoryInfo, sizeof(memoryInfo));
		if (memoryInfo.State == MEM_FREE)
		{
			Log("Memory is free, allocating...\n");
			g_base = VirtualAlloc(g_base, g_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (g_base == nullptr)
			{
				Log("FAIL: VirtualAlloc (GLE=%lu)\n", GetLastError());
				return false;
			}
			Log("VirtualAlloc OK at %p\n", g_base);
		}
		else
		{
			Log("Memory at %p exists (State=0x%lX, Protect=0x%lX). Ensuring PAGE_EXECUTE_READWRITE...\n", g_base, memoryInfo.State, memoryInfo.Protect);
			if (memoryInfo.State != MEM_COMMIT)
			{
				if (!VirtualAlloc(g_base, g_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
				{
					Log("FAIL: VirtualAlloc commit (GLE=%lu)\n", GetLastError());
					return false;
				}
			}
			DWORD oldProtect;
			if (!VirtualProtect(g_base, g_size, PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				Log("FAIL: VirtualProtect (GLE=%lu)\n", GetLastError());
				return false;
			}
		}

		std::memcpy(g_base, cheatBinary, g_size);
		Log("OK: memcpy done\n");
		return true;
	}

	inline bool CallEntryPoint()
	{
		return reinterpret_cast<BOOL(WINAPI*)(HMODULE, DWORD, LPVOID)>(reinterpret_cast<std::uintptr_t>(g_base) + 0x52F64E)(static_cast<HMODULE>(g_base), DLL_PROCESS_ATTACH, nullptr);
	}

	inline bool IsAddressValid(void* address)
	{
		return address >= g_base && address < reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_base) + g_size);
	}
}