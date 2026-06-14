/**
 * @file auto_updater.h
 * @brief Professional Auto-Updater Library for Windows Applications
 * @version 2.0.0
 * @date 2025
 * 
 * @author rabbanyhmm
 * @copyright Copyright (c) 2025 rabbanyhmm. All rights reserved.
 * @license MIT License
 * 
 * GitHub: https://github.com/rabbanyhmm
 * 
 * @description
 * A lightweight, header-only auto-updater library for Windows applications.
 * Features JSON-based version checking, automatic download and installation
 * of updates with seamless application restart.
 * 
 * @dependencies
 * - nlohmann/json library
 * - Windows API (wininet.lib, shell32.lib)
 * - C++11 or later
 * 
 * @usage
 * ```cpp
 * #include "auto_updater.h"
 * 
 * int main() {
 *     if (AutoUpdater::checkForUpdates("1.0.0")) {
 *         std::cout << "Update found and applied!" << std::endl;
 *     } else {
 *         std::cout << "Application is up to date." << std::endl;
 *         // Continue with normal program execution
 *     }
 *     return 0;
 * }
 * ```
 * 
 * @json_format
 * The update server should return JSON in the following format:
 * ```json
 * {
 *     "UpdateLink": "https://example.com/app_v2.0.exe",
 *     "AppVersion": "2.0.0"
 * }
 * ```
 */

#ifndef AUTO_UPDATER_H
#define AUTO_UPDATER_H

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <process.h>
#include "json.hpp" // nlohmann::json library

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

namespace AutoUpdaterLib {

/**
 * @class AutoUpdater
 * @brief Main auto-updater class providing version checking and update functionality
 */
class AutoUpdater {
private:
    std::string m_updateUrl;
    std::string m_currentVersion;
    std::string m_tempDirectory;
    
    static constexpr const char* USER_AGENT = "AutoUpdater/2.0";
    static constexpr DWORD BUFFER_SIZE = 8192;
    static constexpr DWORD TIMEOUT_MS = 30000; // 30 seconds

    /**
     * @brief Downloads a file from the specified URL to local filesystem
     * @param url The URL to download from
     * @param filepath The local path where the file should be saved
     * @return true if download successful, false otherwise
     */
    bool downloadFile(const std::string& url, const std::string& filepath) const {
        HINTERNET hInternet = InternetOpenA(
            USER_AGENT, 
            INTERNET_OPEN_TYPE_DIRECT, 
            nullptr, 
            nullptr, 
            0
        );
        
        if (!hInternet) {
            logError("Failed to initialize internet connection");
            return false;
        }

        HINTERNET hUrl = InternetOpenUrlA(
            hInternet, 
            url.c_str(), 
            nullptr, 
            0, 
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 
            0
        );
        
        if (!hUrl) {
            logError("Failed to open URL: " + url);
            InternetCloseHandle(hInternet);
            return false;
        }

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            logError("Failed to create file: " + filepath);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return false;
        }

        char buffer[BUFFER_SIZE];
        DWORD bytesRead = 0;
        bool success = true;

        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            file.write(buffer, bytesRead);
            if (file.fail()) {
                logError("Failed to write to file: " + filepath);
                success = false;
                break;
            }
        }

        file.close();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        
        return success;
    }

    /**
     * @brief Retrieves and parses JSON data from the update server
     * @param jsonUrl The URL containing version information
     * @return Parsed JSON object, or empty object if failed
     */
    nlohmann::json fetchVersionInfo(const std::string& jsonUrl) const {
        const std::string tempFile = m_tempDirectory + "\\version_info.json";
        nlohmann::json result;

        if (!downloadFile(jsonUrl, tempFile)) {
            return result;
        }

        try {
            std::ifstream file(tempFile);
            if (file.is_open()) {
                file >> result;
                file.close();
            }
        } catch (const std::exception& e) {
            logError("JSON parsing error: " + std::string(e.what()));
        }

        // Cleanup temporary file
        DeleteFileA(tempFile.c_str());
        return result;
    }

    /**
     * @brief Gets the full path of the currently running executable
     * @return Current executable path
     */
    std::string getCurrentExecutablePath() const {
        char path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) {
            throw std::runtime_error("Failed to get current executable path");
        }
        return std::string(path);
    }

    /**
     * @brief Creates and executes a batch file for seamless application update
     * @param newExePath Path to the downloaded update file
     * @param currentExePath Path to the current executable
     */
    void executeUpdate(const std::string& newExePath, const std::string& currentExePath) const {
        const std::string batchPath = m_tempDirectory + "\\updater_script.bat";
        
        std::ofstream batch(batchPath);
        if (!batch.is_open()) {
            throw std::runtime_error("Failed to create update script");
        }

        // Create sophisticated batch script
        batch << "@echo off\n"
              << "title Application Updater\n"
              << "echo Applying update...\n"
              << "timeout /t 3 /nobreak >nul\n"
              << "taskkill /f /im \"" << extractFileName(currentExePath) << "\" >nul 2>&1\n"
              << "timeout /t 1 /nobreak >nul\n"
              << "copy /Y \"" << newExePath << "\" \"" << currentExePath << "\" >nul\n"
              << "if errorlevel 1 (\n"
              << "    echo Update failed!\n"
              << "    pause\n"
              << "    exit /b 1\n"
              << ")\n"
              << "echo Update completed successfully!\n"
              << "start \"\" \"" << currentExePath << "\"\n"
              << "timeout /t 2 /nobreak >nul\n"
              << "del \"" << newExePath << "\" >nul 2>&1\n"
              << "del \"%~f0\" >nul 2>&1\n";

        batch.close();

        // Execute update script
        ShellExecuteA(nullptr, "open", batchPath.c_str(), nullptr, nullptr, SW_HIDE);
        ExitProcess(0);
    }

    /**
     * @brief Extracts filename from full path
     * @param fullPath Full file path
     * @return Filename only
     */
    std::string extractFileName(const std::string& fullPath) const {
        size_t pos = fullPath.find_last_of("\\/");
        return (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
    }

    /**
     * @brief Compares two version strings
     * @param current Current version
     * @param remote Remote version
     * @return true if remote version is newer
     */
    bool isNewerVersion(const std::string& current, const std::string& remote) const {
        // Simple version comparison - can be enhanced for semantic versioning
        return current != remote;
    }

    /**
     * @brief Logs error messages to console
     * @param message Error message to log
     */
    void logError(const std::string& message) const {
        std::cerr << "[AutoUpdater Error] " << message << std::endl;
    }

    /**
     * @brief Logs informational messages to console
     * @param message Information message to log
     */
    void logInfo(const std::string& message) const {
        std::cout << "[AutoUpdater] " << message << std::endl;
    }

public:
    /**
     * @brief Constructs AutoUpdater with specified update URL
     * @param updateUrl URL containing JSON version information
     */
    explicit AutoUpdater(const std::string& updateUrl) : m_updateUrl(updateUrl) {
        // Initialize temporary directory
        char tempPath[MAX_PATH];
        if (GetTempPathA(MAX_PATH, tempPath) == 0) {
            throw std::runtime_error("Failed to get temporary directory");
        }
        
        m_tempDirectory = std::string(tempPath);
        if (!m_tempDirectory.empty() && m_tempDirectory.back() == '\\') {
            m_tempDirectory.pop_back();
        }
    }

    /**
     * @brief Checks for available updates and applies them if found
     * @param currentVersion Current application version
     * @return true if update was found and applied, false otherwise
     */
    bool checkForUpdate(const std::string& currentVersion) {
        m_currentVersion = currentVersion;

        logInfo("Checking for updates...");
        logInfo("Current version: " + m_currentVersion);

        // Fetch version information from server
        nlohmann::json versionInfo = fetchVersionInfo(m_updateUrl);
        if (versionInfo.empty() || !versionInfo.contains("AppVersion") || !versionInfo.contains("UpdateLink")) {
            logError("Invalid or missing version information from server");
            return false;
        }

        std::string remoteVersion;
        std::string updateLink;

        try {
            remoteVersion = versionInfo["AppVersion"].get<std::string>();
            updateLink = versionInfo["UpdateLink"].get<std::string>();
        } catch (const std::exception& e) {
            logError("Failed to parse version information: " + std::string(e.what()));
            return false;
        }

        logInfo("Remote version: " + remoteVersion);

        // Check if update is needed
        if (!isNewerVersion(m_currentVersion, remoteVersion)) {
            logInfo("Application is up to date");
            return false;
        }

        logInfo("Update available! Starting download...");

        // Download update
        const std::string updateFilePath = m_tempDirectory + "\\app_update.exe";
        if (!downloadFile(updateLink, updateFilePath)) {
            logError("Failed to download update");
            return false;
        }

        logInfo("Download completed. Applying update...");

        try {
            executeUpdate(updateFilePath, getCurrentExecutablePath());
        } catch (const std::exception& e) {
            logError("Update execution failed: " + std::string(e.what()));
            return false;
        }

        return true;
    }

    /**
     * @brief Sets a custom temporary directory for update operations
     * @param tempDir Custom temporary directory path
     */
    void setTempDirectory(const std::string& tempDir) {
        m_tempDirectory = tempDir;
    }

    /**
     * @brief Gets the current temporary directory being used
     * @return Current temporary directory path
     */
    std::string getTempDirectory() const {
        return m_tempDirectory;
    }
};

} // namespace AutoUpdaterLib

// Configuration - Update this URL to point to your version JSON endpoint
#ifndef AUTO_UPDATER_CONFIG_URL
#define AUTO_UPDATER_CONFIG_URL "https://pastebin.com/raw/XXXXXXXX"
#endif

/**
 * @brief Convenience function for simple update checking
 * @param version Current application version
 * @param configUrl Optional custom config URL (uses default if empty)
 * @return true if update was found and applied, false otherwise
 * 
 * @example
 * ```cpp
 * if (AutoUpdater::checkForUpdates("1.0.0")) {
 *     // Update found and applied - application will restart
 * } else {
 *     // No update needed - continue normal execution
 * }
 * ```
 */
inline bool checkForUpdates(const std::string& version, const std::string& configUrl = "") {
    try {
        std::string url = configUrl.empty() ? AUTO_UPDATER_CONFIG_URL : configUrl;
        AutoUpdaterLib::AutoUpdater updater(url);
        return updater.checkForUpdate(version);
    } catch (const std::exception& e) {
        std::cerr << "[AutoUpdater Error] " << e.what() << std::endl;
        return false;
    }
}

// Legacy compatibility function
inline bool Updated(const std::string& version) {
    return checkForUpdates(version);
}

#endif // AUTO_UPDATER_H
