/**
 * @file toggle_thread.cpp
 * @brief Implements the background thread for key monitoring and view toggle.
 *
 * Contains main polling loop (GetAsyncKeyState), key state debouncing, and
 * calls to helper functions that use the captured R9 value (`g_r9_for_tpv_flag`)
 * to modify game memory.
 */

#include "toggle_thread.h"
#include "logger.h"
#include "utils.h"
#include "constants.h"

#include <windows.h>
#include <vector>
#include <unordered_map> // For key state debounce map
#include <string>
#include <stdexcept> // For standard exceptions
#include <sstream>

/**
 * @brief (Internal) Safely writes view state byte after validation.
 * @details Core logic: validates R9, calculates address, checks memory
 *          via VirtualQuery, performs read/write if valid.
 * @param new_state Target state (0=FPV, 1=TPV).
 * @param action_desc String describing action for logs.
 * @param key_pressed_vk VK code triggering write.
 * @return bool True if successful/unnecessary, False on failure.
 */
bool setViewState(BYTE new_state, const std::string &action_desc,
                  int key_pressed_vk)
{
    Logger &logger = Logger::getInstance();
    uintptr_t r9_value = 0;

    // 1. Check global pointer validity
    if (!g_r9_for_tpv_flag)
    {
        logger.log(LOG_ERROR, "setViewState(" + action_desc +
                                  "): Global R9 storage ptr is NULL.");
        return false;
    }

    // 2. Read captured R9 value
    r9_value = *g_r9_for_tpv_flag;

    // 3. Validate captured R9 value
    if (r9_value == 0)
    {
        logger.log(LOG_WARNING, "setViewState(" + action_desc +
                                    "): Captured R9 is 0x0. Hook not run or capture failed.");
        return false;
    }
    logger.log(LOG_DEBUG, "setViewState(" + action_desc + "): Using R9 " +
                              format_address(r9_value));

    // 4. Calculate target address
    BYTE *flag_addr = reinterpret_cast<BYTE *>(r9_value +
                                               Constants::TOGGLE_FLAG_OFFSET);
    logger.log(LOG_DEBUG, "setViewState(" + action_desc +
                              "): Calculated flag address: " +
                              format_address(reinterpret_cast<uintptr_t>(flag_addr)));

    // 5. Validate target memory
    MEMORY_BASIC_INFORMATION mem_info = {};
    if (!VirtualQuery(flag_addr, &mem_info, sizeof(mem_info)))
    {
        logger.log(LOG_ERROR, "setViewState(" + action_desc +
                                  "): VirtualQuery failed for target address " +
                                  format_address(reinterpret_cast<uintptr_t>(flag_addr)) +
                                  ". Error: " + std::to_string(GetLastError()) + ".");
        return false;
    }
    bool is_writable = (mem_info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    if (mem_info.State != MEM_COMMIT || !is_writable)
    {
        logger.log(LOG_ERROR, "setViewState(" + action_desc +
                                  "): Target memory " + format_address(reinterpret_cast<uintptr_t>(flag_addr)) + " not committed/writable (State: " +
                                  format_hex(mem_info.State) + ", Protect: " +
                                  format_hex(mem_info.Protect) + ").");
        return false;
    }
    logger.log(LOG_DEBUG, "setViewState(" + action_desc +
                              "): Target memory check passed.");

    // 6. Perform Read/Write Safely
    try
    {
        BYTE current_value = *flag_addr;
        logger.log(LOG_DEBUG, "setViewState(" + action_desc +
                                  "): Read current value: " + std::to_string(current_value));

        if (current_value != new_state)
        {
            *flag_addr = new_state; // Write new value
            logger.log(LOG_INFO, "Action [" + action_desc + "]: Key " +
                                     format_vkcode(key_pressed_vk) + " -> Set TPV Flag to " +
                                     std::to_string(new_state) +
                                     (new_state ? " (ON)" : " (OFF)"));
            logger.log(LOG_DEBUG, "setViewState(" + action_desc +
                                      "): Write successful.");
        }
        else
        {
            logger.log(LOG_DEBUG, "Action [" + action_desc + "]: Key " +
                                      format_vkcode(key_pressed_vk) + ", flag already " +
                                      std::to_string(new_state) + ". No change.");
        }
        return true; // Success (write or no-op)
    }
    catch (const std::exception &e)
    { // Catch basic exceptions
        logger.log(LOG_ERROR, "setViewState(" + action_desc + "): Exception "
                                                              "accessing memory at " +
                                  format_address(
                                      reinterpret_cast<uintptr_t>(flag_addr)) +
                                  ": " + e.what());
        return false;
    }
    catch (...)
    { // Catch any others
        logger.log(LOG_ERROR, "setViewState(" + action_desc +
                                  "): Unknown exception accessing memory at " +
                                  format_address(reinterpret_cast<uintptr_t>(flag_addr)));
        return false;
    }
}

/**
 * @brief Safely toggles TPV state (0 <-> 1). Reads current, calls setViewState.
 */
bool safeToggleViewState(int key_pressed_vk)
{
    Logger &logger = Logger::getInstance();
    uintptr_t r9_value = 0;

    // Prerequisite checks
    if (!g_r9_for_tpv_flag || (r9_value = *g_r9_for_tpv_flag) == 0)
    {
        logger.log(LOG_WARNING, "safeToggleViewState: Cannot toggle, R9 storage"
                                " invalid (Ptr=" +
                                    std::string(g_r9_for_tpv_flag ? "OK" : "NULL") + ", Val=" + format_address(r9_value) + ")");
        return false;
    }

    // Calculate address & Read current state
    BYTE *flag_addr = reinterpret_cast<BYTE *>(r9_value +
                                               Constants::TOGGLE_FLAG_OFFSET);
    BYTE current_value;
    try
    {
        // Simple read check (less reliable than VirtualQuery)
        if (IsBadReadPtr(flag_addr, sizeof(BYTE)))
        {
            throw std::runtime_error("IsBadReadPtr check failed");
        }
        current_value = *flag_addr;
        logger.log(LOG_DEBUG, "safeToggleViewState: Read current value: " +
                                  std::to_string(current_value));
    }
    catch (...)
    { // Catch any issue during read check/read
        logger.log(LOG_ERROR, "safeToggleViewState: Exception reading "
                              "current TPV flag at " +
                                  format_address(
                                      reinterpret_cast<uintptr_t>(flag_addr)) +
                                  ".");
        return false;
    }

    // Determine target state and delegate write/validation to setViewState
    BYTE new_value = (current_value == 0) ? 1 : 0; // Toggle 0->1, 1->0
    logger.log(LOG_DEBUG, "safeToggleViewState: Target state: " +
                              std::to_string(new_value));
    return setViewState(new_value, "Toggle", key_pressed_vk);
}

/**
 * @brief Safely sets view to first-person (state 0).
 */
bool setFirstPersonView(int key_pressed_vk)
{
    return setViewState(0, "Set FPV", key_pressed_vk);
}

/**
 * @brief Safely sets view to third-person (state 1).
 */
bool setThirdPersonView(int key_pressed_vk)
{
    return setViewState(1, "Set TPV", key_pressed_vk);
}

/**
 * @brief Background thread function monitoring keys via GetAsyncKeyState.
 * @param param Expected to be `ToggleData*`, ownership transferred.
 */
DWORD WINAPI ToggleThread(LPVOID param)
{
    // --- Initialization ---
    ToggleData *data_ptr = static_cast<ToggleData *>(param);
    if (!data_ptr)
    {
        Logger::getInstance().log(LOG_ERROR, "ToggleThread: Received NULL "
                                             "data. Thread exiting.");
        return 1;
    }
    std::vector<int> toggle_keys = std::move(data_ptr->toggle_keys);
    std::vector<int> fpv_keys = std::move(data_ptr->fpv_keys);
    std::vector<int> tpv_keys = std::move(data_ptr->tpv_keys);
    delete data_ptr; // Delete struct now that data is moved

    Logger &logger = Logger::getInstance();
    logger.log(LOG_INFO, "ToggleThread: Monitoring thread started.");

    // --- Check if Monitoring Needed ---
    bool noop = toggle_keys.empty() && fpv_keys.empty() && tpv_keys.empty();
    if (noop)
    {
        logger.log(LOG_INFO, "ToggleThread: No keys configured. Idling.");
        while (true)
        {
            Sleep(5000);
        } // Infinite idle loop
        // return 0; // Unreachable
    }

    logger.log(LOG_INFO, "ToggleThread: Keys: Toggle=" +
                             format_vkcode_list(toggle_keys));
    logger.log(LOG_INFO, "ToggleThread: Keys: FPV=" +
                             format_vkcode_list(fpv_keys));
    logger.log(LOG_INFO, "ToggleThread: Keys: TPV=" +
                             format_vkcode_list(tpv_keys));

    // --- Debounce Map ---
    // Tracks previous key state (true=down) for edge detection
    std::unordered_map<int, bool> key_was_down;
    for (int vk : toggle_keys)
        key_was_down[vk] = false;
    for (int vk : fpv_keys)
        key_was_down[vk] = false;
    for (int vk : tpv_keys)
        key_was_down[vk] = false;

    // --- Main Polling Loop ---
    logger.log(LOG_INFO, "ToggleThread: Entering key polling loop...");
    while (true)
    {
        try
        {
            bool action = false; // Track if any key was actioned this cycle

            // --- Check Keys & Trigger Actions ---
            for (int vk : toggle_keys)
            { // Toggle keys
                bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (down && !key_was_down[vk])
                { // Just pressed
                    logger.log(LOG_DEBUG, "Input: Toggle Key " +
                                              format_vkcode(vk) + " pressed.");
                    safeToggleViewState(vk);
                    action = true;
                }
                key_was_down[vk] = down; // Update state
            }
            for (int vk : fpv_keys)
            { // FPV keys
                bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (down && !key_was_down[vk])
                {
                    logger.log(LOG_DEBUG, "Input: FPV Key " +
                                              format_vkcode(vk) + " pressed.");
                    setFirstPersonView(vk);
                    action = true;
                }
                key_was_down[vk] = down;
            }
            for (int vk : tpv_keys)
            { // TPV keys
                bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (down && !key_was_down[vk])
                {
                    logger.log(LOG_DEBUG, "Input: TPV Key " +
                                              format_vkcode(vk) + " pressed.");
                    setThirdPersonView(vk);
                    action = true;
                }
                key_was_down[vk] = down;
            }

            // --- Adaptive Sleep ---
            Sleep(action ? 15 : 50); // ms; shorter sleep if key pressed
        }
        catch (const std::exception &e)
        { // Catch errors within loop
            logger.log(LOG_ERROR, "ToggleThread: Exception in polling loop: " +
                                      std::string(e.what()));
            Sleep(1000); // Longer pause after error
        }
        catch (...)
        {
            logger.log(LOG_ERROR, "ToggleThread: Unknown exception in loop.");
            Sleep(1000);
        }
    } // End while(true)
}
