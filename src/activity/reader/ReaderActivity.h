#pragma once

#include <memory>
#include <functional>
#include <string>

#include "../ActivityWithSubactivity.h"
#include "activity/page/LibraryActivity.h"

// Forward declarations
class Epub;
class Xtc;
class Txt;

/**
 * @brief Activity responsible for detecting file type and launching the appropriate reader
 * 
 * ReaderActivity acts as a dispatcher that examines the file extension of a given book
 * and transitions to the specific reader activity (EPUB, XTC, or TXT). It handles loading
 * the file and error cases, providing a unified entry point for all reading activities.
 */
class ReaderActivity final : public ActivityWithSubactivity {
private:
    std::string initialBookPath;                    ///< Path to the book file to open
    std::string currentBookPath;                     ///< Path of currently loaded book for navigation
    const std::function<void(const std::string&)> onGoBack;          ///< Callback to return to previous activity

    /**
     * @brief Loads an EPUB file from the given path
     * @param path Path to the EPUB file
     * @return Unique pointer to loaded Epub object, or nullptr if loading fails
     */
    static std::unique_ptr<Epub> loadEpub(const std::string& path);

    /**
     * @brief Loads an XTC file from the given path
     * @param path Path to the XTC file
     * @return Unique pointer to loaded Xtc object, or nullptr if loading fails
     */
    static std::unique_ptr<Xtc> loadXtc(const std::string& path);

    /**
     * @brief Loads a TXT file from the given path
     * @param path Path to the TXT file
     * @return Unique pointer to loaded Txt object, or nullptr if loading fails
     */
    static std::unique_ptr<Txt> loadTxt(const std::string& path);

    /**
     * @brief Checks if the file is an XTC format file
     * @param path Path to the file
     * @return true if file has .xtc or .xtch extension
     */
    static bool isXtcFile(const std::string& path);

    /**
     * @brief Checks if the file is a plain text file
     * @param path Path to the file
     * @return true if file has .txt or .md extension
     */
    static bool isTxtFile(const std::string& path);

    /**
     * @brief Extracts the parent directory path from a file path
     * @param filePath Full path to a file
     * @return Path to the containing directory
     */
    static std::string extractFolderPath(const std::string& filePath);

    /**
     * @brief Navigates to library view starting from the folder containing the specified book
     * @param fromBookPath Path to a book file, or empty string to start from root
     */
    void goToLibrary(const std::string& fromBookPath = "");

    /**
     * @brief Transitions to the EPUB reader activity with the loaded EPUB
     * @param epub Unique pointer to loaded Epub object
     */
    void onGoToEpubReader(std::unique_ptr<Epub> epub);

    /**
     * @brief Transitions to the XTC reader activity with the loaded XTC
     * @param xtc Unique pointer to loaded Xtc object
     */
    void onGoToXtcReader(std::unique_ptr<Xtc> xtc);

    /**
     * @brief Transitions to the TXT reader activity with the loaded TXT
     * @param txt Unique pointer to loaded Txt object
     */
    void onGoToTxtReader(std::unique_ptr<Txt> txt);

public:
    /**
     * @brief Constructor for ReaderActivity
     * @param renderer Reference to the graphics renderer
     * @param mappedInput Reference to the input manager
     * @param initialBookPath Path to the book file to open (can be empty for library navigation)
     * @param onGoBack Callback function to return to previous activity
     * @param onGoToCallback Callback function to open library at specified path
     */
    explicit ReaderActivity(
        GfxRenderer& renderer,
        MappedInputManager& mappedInput,
        const std::string& initialBookPath,
        std::function<void(const std::string&)> onGoBack
    ) : ActivityWithSubactivity("Reader", renderer, mappedInput),
        initialBookPath(initialBookPath),
        onGoBack(std::move(onGoBack)){}

    /**
     * @brief Called when entering the reader activity
     * 
     * Handles the initial book path and loads the appropriate reader based on file type.
     * If no book path is provided, navigates to the library view.
     * If file loading fails, returns to the previous activity.
     */
    void onEnter() override;
};