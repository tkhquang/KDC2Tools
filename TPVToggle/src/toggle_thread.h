/**
 * @file toggle_thread.h
 * @brief Header for background key monitoring and TPV toggle logic thread.
 *
 * Declares data structures, thread entry function, and view state manipulation
 * functions using the globally captured R9 pointer.
 */
#ifndef TOGGLE_THREAD_H
#define TOGGLE_THREAD_H

#include <windows.h>
#include <vector>
#include <string>
#include <cstdint> // For uintptr_t

/**
 * @brief Global pointer (extern "C" linked) to storage for captured R9.
 * @details Defined/managed in dllmain.cpp, written by assembly hook, read
 * by this thread. Ensures C-style linkage compatibility.
 */
extern "C" uintptr_t *g_r9_for_tpv_flag;

/**
 * @struct ToggleData
 * @brief Structure holding configured key codes passed to the toggle thread.
 * @details Thread takes ownership of the allocated instance.
 */
struct ToggleData
{
    std::vector<int> toggle_keys; /**< VK codes for toggling FPV/TPV. */
    std::vector<int> fpv_keys;    /**< VK codes for forcing FPV. */
    std::vector<int> tpv_keys;    /**< VK codes for forcing TPV. */
};

/**
 * @brief Background thread function entry point for key monitoring.
 * @details Loops infinitely polling GetAsyncKeyState and calling view change
 * functions based on configured keys. Takes ownership of `param`.
 * @param param `LPVOID` expected to be a `new ToggleData*`.
 * @return DWORD Typically 0 (loops forever unless setup fails).
 */
DWORD WINAPI ToggleThread(LPVOID param);

/**
 * @brief Safely attempts toggle TPV state (0 <-> 1) using captured R9 value.
 * @param key_pressed_vk VK code of trigger key (for logging).
 * @return `true` if action initiated successfully (write or no-op),
 *         `false` if prerequisites failed (e.g., null R9 value).
 */
bool safeToggleViewState(int key_pressed_vk);

/**
 * @brief Safely attempts setting view state to first-person (0).
 * @param key_pressed_vk VK code of trigger key (for logging).
 * @return `true` if action initiated, `false` on failure.
 */
bool setFirstPersonView(int key_pressed_vk);

/**
 * @brief Safely attempts setting view state to third-person (1).
 * @param key_pressed_vk VK code of trigger key (for logging).
 * @return `true` if action initiated, `false` on failure.
 */
bool setThirdPersonView(int key_pressed_vk);

/**
 * @brief (Internal) Core function to set view state byte in memory.
 * @details Validates R9 ptr, target addr, memory state; performs write.
 * @param new_state Desired byte state (0=FPV, 1=TPV).
 * @param action_desc Logging description (e.g., "Toggle").
 * @param key_pressed_vk VK code triggering the action.
 * @return `true` if write successful/unnecessary, `false` on error.
 */
bool setViewState(BYTE new_state, const std::string &action_desc,
                  int key_pressed_vk);

#endif // TOGGLE_THREAD_H
