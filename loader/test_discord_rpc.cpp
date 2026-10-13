#include <windows.h>
#include <iostream>
#include "discord_rpc.hpp"

int main()
{
    SetConsoleTitleA("Primordial Discord RPC Test");

    std::cout << "========================================" << std::endl;
    std::cout << "      Discord Rich Presence Test        " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Title:   Playing Primordial" << std::endl;
    std::cout << "  State:   @saved_messenges" << std::endl;
    std::cout << "  Timer:   Elapsed time counter active" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[*] Connecting to Discord IPC Pipe..." << std::endl;

    g_DiscordRPC.Start();

    std::cout << "[+] Discord RPC is now ACTIVE!" << std::endl;
    std::cout << "[i] Open Discord to see your active game status." << std::endl;
    std::cout << "\nPress ENTER to stop test and exit..." << std::endl;
    
    std::cin.get();

    std::cout << "[*] Stopping Discord RPC..." << std::endl;
    g_DiscordRPC.Stop();
    std::cout << "[+] Done!" << std::endl;
    return 0;
}
