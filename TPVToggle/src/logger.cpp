/**
 * @file logger.cpp
 * @brief Implementation of the singleton Logger class.
 */
#include "logger.h" // Corresponding header

/**
 * @brief Private Logger constructor. Sets default level, finds path, opens stream.
 */
Logger::Logger() : current_log_level(LOG_INFO) // Default level
{
    std::string log_file_path = generateLogFilePath();

    // Open file, overwriting existing content (truncate).
    log_file_stream.open(log_file_path, std::ios::trunc);

    if (!log_file_stream.is_open())
    {
        std::cerr << "[" << Constants::MOD_NAME << " Logger ERROR] "
                  << "Failed open log file: " << log_file_path << std::endl;
    }
    else
    {
        // Log successful initialization to the file itself.
        log_file_stream << "[" << getTimestamp() << "] "
                        << "[INFO   ] :: Logger initialized. Log file: "
                        << log_file_path << std::endl;
    }
}

/**
 * @brief Logger destructor. Flushes and closes the log file stream.
 */
Logger::~Logger()
{
    if (log_file_stream.is_open())
    {
        log_file_stream << "[" << getTimestamp() << "] "
                        << "[INFO   ] :: Logger shutting down." << std::endl;
        log_file_stream.flush();
        log_file_stream.close();
    }
}

/**
 * @brief Sets the minimum logging level.
 * @param level New minimum LogLevel.
 */
void Logger::setLogLevel(LogLevel level)
{
    current_log_level = level;
    log(LOG_DEBUG, "Log level set to: " + std::to_string(level));
}

/**
 * @brief Gets current timestamp string ("YYYY-MM-DD HH:MM:SS").
 * @return std::string Formatted time or "TIMESTAMP_ERR".
 */
std::string Logger::getTimestamp() const
{
    try
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm timeinfo = {}; // Zero-initialize

#ifdef _MSC_VER
        if (localtime_s(&timeinfo, &in_time_t) != 0)
        {
            throw std::runtime_error("localtime_s failed");
        }
#else // Standard C++ / MinGW
        std::tm *timeinfo_ptr = std::localtime(&in_time_t);
        if (!timeinfo_ptr)
        {
            throw std::runtime_error("std::localtime returned null");
        }
        timeinfo = *timeinfo_ptr; // Copy data if successful
#endif
        std::ostringstream oss;
        oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Logger Timestamp Error: " << e.what() << std::endl;
        return "TIMESTAMP_ERR";
    }
    catch (...)
    {
        std::cerr << "Logger Timestamp Error: Unknown exception." << std::endl;
        return "TIMESTAMP_ERR";
    }
}

/**
 * @brief Determines log file path using DLL location. Falls back to base name.
 * @return std::string Full path for the log file.
 */
std::string Logger::generateLogFilePath() const
{
    std::string base_filename = Constants::getLogFilename();
    std::string result_path = base_filename; // Fallback value

    try
    {
        char dll_path_buffer[MAX_PATH] = {0};
        HMODULE h_self = NULL;

        // Get module handle for this DLL.
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)&Logger::getInstance, &h_self))
        {
            throw std::runtime_error("GetModuleHandleExA failed: " +
                                     std::to_string(GetLastError()));
        }

        // Get full path of the loaded DLL. Check for errors.
        DWORD path_len = GetModuleFileNameA(h_self, dll_path_buffer, MAX_PATH);
        if (path_len == 0 || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            throw std::runtime_error("GetModuleFileNameA failed: " +
                                     std::to_string(GetLastError()));
        }

        // Construct path: <DLL_Directory>/<Base_Log_Filename>
        std::filesystem::path dll_full_path(dll_path_buffer);
        std::filesystem::path log_file = dll_full_path.parent_path() / base_filename;
        result_path = log_file.string();
    }
    catch (const std::exception &e)
    {
        // Log failure to standard error, keep using fallback path.
        std::cerr << "Logger Path Error determining DLL location: " << e.what()
                  << ". Using log path: " << result_path << std::endl;
    }
    catch (...)
    {
        std::cerr << "Logger Path Error: Unknown exception determining path."
                  << " Using log path: " << result_path << std::endl;
    }

    return result_path;
}

/**
 * @brief Writes message to log file if level threshold met. Uses stderr fallback.
 * @param level Severity level of message.
 * @param message Content to log.
 */
void Logger::log(LogLevel level, const std::string &message)
{
    // Only proceed if level is sufficient and file stream is usable.
    if (level >= current_log_level && log_file_stream.is_open() &&
        log_file_stream.good())
    {
        std::string level_str;
        switch (level)
        {
        case LOG_DEBUG:
            level_str = "DEBUG";
            break;
        case LOG_INFO:
            level_str = "INFO";
            break;
        case LOG_WARNING:
            level_str = "WARNING";
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
            break;
        }

        // Write structured log entry. std::endl flushes buffer.
        log_file_stream << "[" << getTimestamp() << "] "
                        << "[" << std::setw(7) << std::left << level_str << "] :: "
                        << message << std::endl;
    }
    // Fallback to standard error for ERROR messages if file stream is bad.
    else if (level == LOG_ERROR && !log_file_stream.is_open())
    {
        std::cerr << "[LOG_FILE_ERROR] [" << getTimestamp() << "] [ERROR] :: "
                  << message << std::endl;
    }
}
