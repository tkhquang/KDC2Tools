/**
 * @file dllmain.cpp
 * @brief Main entry point and initialization logic for the TPVToggle mod.
 *
 * Handles DLL attach/detach, sets up logging, config loading, AOB scanning,
 * MinHook initialization, hook placement, and starts the background key
 * monitoring thread.
 */

#include "aob_scanner.h"
#include "toggle_thread.h"
#include "logger.h"
#include "config.h"
#include "utils.h"
#include "constants.h"
#include "version.h"
#include "MinHook.h"

#include <windows.h>
#include <psapi.h> // For GetModuleInformation
#include <string>
#include <vector>
#include <iomanip>
#include <thread>
#include <chrono> // For std::chrono::seconds

// --- Global Variables for Hooking ---
// Need extern "C" for stable linkage with assembly code.
extern "C"
{
    /**
     * @brief Global ptr to allocated memory storing captured R9 value.
     * Assembly writes the TPV context ptr here; Toggle thread reads it.
     * Initialized NULL, allocated in MainThread, freed in CleanupResources.
     */
    uintptr_t *g_r9_for_tpv_flag = nullptr;

    /**
     * @brief Global fn ptr storing original code continuation address.
     * Set by MinHook; assembly hook jumps here after its detour logic.
     * Initialized NULL.
     */
    void *fpTPV_OriginalCode = nullptr;
}
// --- End Extern "C" ---

/**
 * @brief Global ptr storing the absolute memory address where the hook is placed.
 * Set after AOB scan. Used by MinHook enable/disable/remove calls.
 */
BYTE *g_tpvHookAddress = nullptr;

/**
 * @brief External declaration of the assembly detour function.
 * Implemented in `src/asm/tpv_view_hook.S`. Linked via extern "C".
 */
extern "C" void TPV_CaptureR9_Detour();

/**
 * @brief Cleans up resources (hooks, allocated memory) on DLL unload/failure.
 * Attempts disable/remove hooks, uninit MinHook, free allocated memory.
 * Safe to call even if initialization was partial.
 */
void CleanupResources()
{
    Logger &logger = Logger::getInstance();
    logger.log(LOG_INFO, "Cleanup: Starting resource cleanup...");

    // --- Disable & Remove MinHook Hook ---
    // Check if hook appears to have been successfully created
    if (g_tpvHookAddress && fpTPV_OriginalCode)
    {
        MH_STATUS mh_stat = MH_DisableHook(g_tpvHookAddress);
        if (mh_stat == MH_OK || mh_stat == MH_ERROR_DISABLED)
        {
            logger.log(LOG_INFO, "Cleanup: MinHook hook disabled.");
            // Only remove if disable was OK
            mh_stat = MH_RemoveHook(g_tpvHookAddress);
            if (mh_stat == MH_OK)
            {
                logger.log(LOG_INFO, "Cleanup: MinHook hook removed.");
            }
            else
            {
                logger.log(LOG_ERROR, "Cleanup: Failed to remove MinHook hook: " + std::string(MH_StatusToString(mh_stat)));
            }
        }
        else
        {
            logger.log(LOG_ERROR, "Cleanup: Failed to disable MinHook hook: " + std::string(MH_StatusToString(mh_stat)));
        }
    }
    else
    {
        logger.log(LOG_DEBUG, "Cleanup: Hook not installed or already "
                              "cleaned, skipping disable/remove.");
    }

    // --- Uninitialize MinHook ---
    // Attempt only if it was likely initialized (hook target address found)
    if (g_tpvHookAddress)
    {
        MH_STATUS mh_stat = MH_Uninitialize();
        // Log outcome, allowing MH_ERROR_NOT_INITIALIZED during shutdown
        if (mh_stat == MH_OK || mh_stat == MH_ERROR_NOT_INITIALIZED)
        {
            logger.log(LOG_INFO, "Cleanup: MinHook uninitialize "
                                 "attempted (Status: " +
                                     std::string(MH_StatusToString(mh_stat)) + ")");
        }
        else
        {
            logger.log(LOG_ERROR, "Cleanup: Failed to uninitialize MinHook: " +
                                      std::string(MH_StatusToString(mh_stat)));
        }
    }

    // --- Free Allocated Memory ---
    // Release the memory used for storing the captured R9 pointer
    if (g_r9_for_tpv_flag)
    {
        logger.log(LOG_DEBUG, "Cleanup: Freeing R9 storage memory at " +
                                  format_address(reinterpret_cast<uintptr_t>(
                                      g_r9_for_tpv_flag)));
        // VirtualFree with MEM_RELEASE requires size 0 for MEM_COMMIT|RESERVE
        if (!VirtualFree(g_r9_for_tpv_flag, 0, MEM_RELEASE))
        {
            logger.log(LOG_ERROR, "Cleanup: VirtualFree failed for R9 storage."
                                  " Error: " +
                                      std::to_string(GetLastError()));
        }
        g_r9_for_tpv_flag = nullptr; // Nullify pointer after freeing
    }
    else
    {
        logger.log(LOG_DEBUG, "Cleanup: R9 storage already freed or "
                              "not allocated.");
    }

    // Reset global pointers to a clean state
    g_tpvHookAddress = nullptr;
    fpTPV_OriginalCode = nullptr;
    logger.log(LOG_INFO, "Cleanup: Resource cleanup finished.");
}

/**
 * @brief Main initialization function, runs in a separate thread on DLL attach.
 * @param hModule_param Module handle (unused).
 * @return DWORD 0 on success, non-zero on fatal error preventing mod function.
 */
DWORD WINAPI MainThread(LPVOID hModule_param)
{
    (void)hModule_param; // Unused parameter

    // Phase 1: Basic Setup
    Logger &logger = Logger::getInstance(); // Init/get logger
    logger.log(LOG_INFO, "--------------------");
    Version::logVersionInfo();                                  // Log mod version info
    Config config = loadConfig(Constants::getConfigFilename()); // Load INI

    // Set global log level from config
    LogLevel log_level = LOG_INFO;
    if (config.log_level == "DEBUG")
        log_level = LOG_DEBUG;
    else if (config.log_level == "INFO")
        log_level = LOG_INFO; // Default check
    else if (config.log_level == "WARNING")
        log_level = LOG_WARNING;
    else if (config.log_level == "ERROR")
        log_level = LOG_ERROR;
    logger.setLogLevel(log_level);

    logger.log(LOG_INFO, "MainThread: Initializing mod...");
    logger.log(LOG_INFO, "Settings: ToggleKeys: " +
                             format_vkcode_list(config.toggle_keys));
    logger.log(LOG_INFO, "Settings: FPVKeys: " +
                             format_vkcode_list(config.fpv_keys));
    logger.log(LOG_INFO, "Settings: TPVKeys: " +
                             format_vkcode_list(config.tpv_keys));
    logger.log(LOG_INFO, "Settings: LogLevel: " + config.log_level);
    // Potentially long AOB string - log maybe at DEBUG level?
    logger.log(LOG_DEBUG, "Settings: AOBPattern: " + config.aob_pattern);
    logger.log(LOG_INFO, "Settings: Hook Offset: +" +
                             std::to_string(Constants::HOOK_OFFSET));
    logger.log(LOG_INFO, "Settings: Flag Offset (from R9): +" +
                             format_hex(Constants::TOGGLE_FLAG_OFFSET));

    // Phase 2: Memory Allocation & Target Module Identification
    g_r9_for_tpv_flag = reinterpret_cast<uintptr_t *>(
        VirtualAlloc(NULL, sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    if (!g_r9_for_tpv_flag)
    {
        logger.log(LOG_ERROR, "Fatal: VirtualAlloc failed for R9 storage. "
                              "Err: " +
                                  std::to_string(GetLastError()));
        return 1; // Critical failure
    }
    *g_r9_for_tpv_flag = 0; // Init stored pointer value to NULL
    logger.log(LOG_DEBUG, "MainThread: Allocated R9 storage: " +
                              format_address(reinterpret_cast<uintptr_t>(g_r9_for_tpv_flag)));

    // Find target game module, retry briefly for late loaders
    logger.log(LOG_INFO, "MainThread: Searching for module '" +
                             std::string(Constants::MODULE_NAME) + "'...");
    HMODULE game_module = NULL;
    for (int i = 0; i < 30 && !game_module; ++i)
    {
        game_module = GetModuleHandleA(Constants::MODULE_NAME);
        if (!game_module)
        {
            if (i == 0)
            { // Log only on first retry attempt
                logger.log(LOG_WARNING, "Module not found yet, retrying...");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    if (!game_module)
    {
        logger.log(LOG_ERROR, "Fatal: Module '" +
                                  std::string(Constants::MODULE_NAME) + "' not found after timeout.");
        CleanupResources();
        return 1;
    }
    logger.log(LOG_INFO, "MainThread: Found module '" +
                             std::string(Constants::MODULE_NAME) + "' at " +
                             format_address(reinterpret_cast<uintptr_t>(game_module)));

    MODULEINFO mod_info = {};
    if (!GetModuleInformation(GetCurrentProcess(), game_module, &mod_info,
                              sizeof(mod_info)))
    {
        logger.log(LOG_ERROR, "Fatal: GetModuleInformation failed. Err: " +
                                  std::to_string(GetLastError()));
        CleanupResources();
        return 1;
    }
    logger.log(LOG_DEBUG, "MainThread: Module size: " +
                              format_address(mod_info.SizeOfImage));

    // Phase 3: AOB Scan and Hook Setup
    logger.log(LOG_INFO, "MainThread: Parsing AOB pattern...");
    std::vector<BYTE> pattern = parseAOB(config.aob_pattern);
    if (pattern.empty())
    {
        logger.log(LOG_ERROR, "Fatal: AOB pattern parsing failed or empty.");
        CleanupResources();
        return 1;
    }

    logger.log(LOG_INFO, "MainThread: Scanning module for AOB pattern...");
    BYTE *base = static_cast<BYTE *>(mod_info.lpBaseOfDll);
    BYTE *pattern_start = FindPattern(base, mod_info.SizeOfImage, pattern);
    if (!pattern_start)
    {
        logger.log(LOG_ERROR, "Fatal: AOB pattern not found. Verify pattern in"
                              " INI matches game version.");
        CleanupResources();
        return 1;
    }
    logger.log(LOG_INFO, "MainThread: Found AOB pattern at: " +
                             format_address(reinterpret_cast<uintptr_t>(pattern_start)));

    // Calculate exact hook address
    g_tpvHookAddress = pattern_start + Constants::HOOK_OFFSET;
    logger.log(LOG_INFO, "MainThread: Calculated hook target address: " +
                             format_address(reinterpret_cast<uintptr_t>(g_tpvHookAddress)));

    // Phase 4: Initialize MinHook and Create/Enable Hook
    logger.log(LOG_INFO, "MainThread: Initializing MinHook...");
    MH_STATUS mh_stat = MH_Initialize();
    if (mh_stat != MH_OK)
    {
        logger.log(LOG_ERROR, "Fatal: MH_Initialize failed: " +
                                  std::string(MH_StatusToString(mh_stat)));
        CleanupResources(); // Frees R9 storage too
        return 1;
    }

    logger.log(LOG_INFO, "MainThread: Creating TPV R9 capture hook...");
    mh_stat = MH_CreateHook(g_tpvHookAddress,
                            reinterpret_cast<LPVOID>(&TPV_CaptureR9_Detour),
                            &fpTPV_OriginalCode);
    if (mh_stat != MH_OK)
    {
        logger.log(LOG_ERROR, "Fatal: MH_CreateHook failed: " +
                                  std::string(MH_StatusToString(mh_stat)));
        MH_Uninitialize(); // Uninit if init succeeded but create failed
        CleanupResources();
        return 1;
    }
    if (!fpTPV_OriginalCode)
    { // Should not occur if MH_OK
        logger.log(LOG_ERROR, "Fatal: MH_CreateHook ok but continuation NULL");
        MH_RemoveHook(g_tpvHookAddress); // Attempt removal
        MH_Uninitialize();
        CleanupResources();
        return 1;
    }
    logger.log(LOG_DEBUG, "MainThread: Hook created. Continuation address: " +
                              format_address(reinterpret_cast<uintptr_t>(fpTPV_OriginalCode)));

    logger.log(LOG_INFO, "MainThread: Enabling TPV hook...");
    mh_stat = MH_EnableHook(g_tpvHookAddress);
    if (mh_stat != MH_OK)
    {
        logger.log(LOG_ERROR, "Fatal: MH_EnableHook failed: " +
                                  std::string(MH_StatusToString(mh_stat)));
        MH_RemoveHook(g_tpvHookAddress); // Cleanup created hook
        MH_Uninitialize();
        CleanupResources();
        return 1;
    }
    logger.log(LOG_INFO, "MainThread: Hook enabled successfully.");

    // Phase 5: Start Key Monitoring Thread
    logger.log(LOG_INFO, "MainThread: Starting key monitoring thread...");
    // Move key data into struct for thread, allocate struct on heap
    ToggleData *thread_data = new ToggleData{
        std::move(config.toggle_keys),
        std::move(config.fpv_keys),
        std::move(config.tpv_keys)};
    HANDLE h_thread = CreateThread(NULL, 0, ToggleThread, thread_data, 0, NULL);
    if (!h_thread)
    {
        logger.log(LOG_ERROR, "Fatal: CreateThread failed for monitor thread."
                              " Error: " +
                                  std::to_string(GetLastError()));
        // Full cleanup: disable/remove hook, uninit, free memory
        MH_DisableHook(g_tpvHookAddress);
        MH_RemoveHook(g_tpvHookAddress);
        MH_Uninitialize();
        CleanupResources();
        delete thread_data; // Delete data since thread didn't take ownership
        return 1;
    }
    CloseHandle(h_thread); // Detach handle, let thread run independently
    logger.log(LOG_INFO, "MainThread: Initialization successful. Mod active.");

    return 0; // Success
}

/**
 * @brief Standard Windows DLL entry point. Initializes/cleans up the mod.
 * @param hModule Handle to this DLL module.
 * @param reason_for_call Attach/Detach reason code.
 * @param lpReserved Reserved.
 * @return BOOL TRUE on success, FALSE on attach failure.
 */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason_for_call, LPVOID lpReserved)
{
    (void)lpReserved; // Unused

    switch (reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        // Slightly optimize by disabling thread attach/detach calls
        DisableThreadLibraryCalls(hModule);

        // Start the main initialization sequence in a new thread
        HANDLE h_main = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
        if (!h_main)
        {
            // Critical: Failed to start initialization
            MessageBoxA(NULL, "FATAL: Failed create initialization thread!",
                        Constants::MOD_NAME, MB_ICONERROR | MB_OK);
            return FALSE; // Fail DLL load
        }
        // Don't need to wait, close handle immediately
        CloseHandle(h_main);
        break;
    }
    case DLL_PROCESS_DETACH:
        // Process is unloading DLL (e.g., game exit)
        CleanupResources(); // Perform cleanup
        break;

    case DLL_THREAD_ATTACH: // Fall through
    case DLL_THREAD_DETACH:
        // Not needed for this mod
        break;
    }
    return TRUE; // Success
}
