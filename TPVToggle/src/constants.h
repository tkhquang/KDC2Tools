/**
 * @file constants.h
 * @brief Central definitions for constants used throughout the mod.
 *
 * Includes version info, filenames, default settings, memory offsets, AOB
 * patterns. Uses a namespace for organization.
 */
#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>
#include "version.h" // Mod versioning definitions

/**
 * @namespace Constants
 * @brief Encapsulates global constants and config defaults.
 */
namespace Constants
{
    // Version information derived from version.h
    constexpr const char *MOD_VERSION = Version::VERSION_STRING;
    constexpr const char *MOD_NAME = Version::MOD_NAME;
    constexpr const char *MOD_WEBSITE = Version::REPOSITORY;

    // File extensions
    constexpr const char *INI_FILE_EXTENSION = ".ini";
    constexpr const char *LOG_FILE_EXTENSION = ".log";

    /**
     * @brief Gets the expected INI config filename (e.g.,
     * "KCD2_TPVToggle.ini").
     * @return std::string The config filename.
     */
    inline std::string getConfigFilename()
    {
        return std::string(MOD_NAME) + INI_FILE_EXTENSION;
    }

    /**
     * @brief Gets the base log filename (e.g., "KCD2_TPVToggle.log").
     * @details Actual path determined by Logger using DLL location.
     * @return std::string The log filename base.
     */
    inline std::string getLogFilename()
    {
        return std::string(MOD_NAME) + LOG_FILE_EXTENSION;
    }

    // --- Default Configuration Values ---

    /** @brief Default logging level ("INFO"). Used if INI missing/invalid. */
    constexpr const char *DEFAULT_LOG_LEVEL = "INFO";

    // --- AOB (Array-of-Bytes) Patterns ---

    /**
     * @brief Default AOB pattern to find the TPV view context code.
     * @details Targets the sequence including `mov r9,[rax+38]`.
     * Ensure this pattern matches the target game version.
     *
     * Sequence Breakdown:
     *   48 8B 8F 58 0A 00 00  ; mov rcx, [rdi+...]
     *   48 83 C1 10           ; add rcx, 10
     *   4C 8B 48 38           ; mov r9, [rax+38]   <-- HOOK TARGET (+11)
     *   4C 8B 01              ; mov r8, [rcx]
     *   41 8A 41 38           ; mov al, [r9+38]    <-- TPV Flag Read
     *   F6 D8                 ; neg al
     *   48 1B D2              ; sbb rdx, rdx
     */
    constexpr const char *DEFAULT_AOB_PATTERN =
        "48 8B 8F 58 0A 00 00 48 83 C1 10 4C 8B 48 38 " // Line 1
        "4C 8B 01 41 8A 41 38 F6 D8 48 1B D2";          // Line 2

    // --- Memory Offsets ---

    /**
     * @brief Offset (bytes) from AOB start to `mov r9,[rax+38]` hook target.
     */
    constexpr int HOOK_OFFSET = 11;

    /**
     * @brief Offset (bytes) from captured R9 pointer to the TPV flag byte.
     * @details Flag: 0 = FPV, 1 = TPV. Relative to the captured R9 value.
     */
    constexpr int TOGGLE_FLAG_OFFSET = 0x38;

    /** @brief Name of the game module to scan. */
    constexpr const char *MODULE_NAME = "WHGame.dll";

} // namespace Constants

#endif // CONSTANTS_H
