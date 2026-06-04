/// @file terminal.hpp
/// @brief Text-mode terminal on top of the framebuffer.

#pragma once

#include <types.hpp>

namespace service {

/// @brief Text terminal with cursor, scrolling, and line editing.
/// @note Renders to the framebuffer using the built-in bitmap font.
class Terminal {
public:
    /// @brief Initialises the terminal and clears the screen.
    static void init();
    /// @brief Writes a single character to the terminal.
    /// @param c Character to display.
    static void putchar(char c);
    /// @brief Writes a null-terminated string to the terminal.
    /// @param str String to display.
    static void write(const char* str);
    /// @brief Writes a buffer of given length to the terminal.
    /// @param data Pointer to the data buffer.
    /// @param len  Number of bytes to write.
    static void write(const char* data, size_t len);

    /// @brief Reads a line of input from the keyboard into a buffer.
    /// @param buf     Output buffer for the line.
    /// @param max_len Maximum buffer size (including null terminator).
    /// @return True if a line was read.
    static bool readline(char* buf, size_t max_len);
    /// @brief Writes a string followed by a newline.
    /// @param str String to display.
    static void puts(const char* str);

    /// @brief Sets the foreground (text) color.
    /// @param color 24-bit RGB color.
    static void set_fg(uint32_t color);
    /// @brief Sets the background color.
    /// @param color 24-bit RGB color.
    static void set_bg(uint32_t color);

    /// @brief Clears the entire terminal screen.
    static void clear();
    /// @brief Scrolls the terminal content up by one line.
    static void scroll();

    /// @brief Returns the singleton terminal instance.
    /// @return Pointer to the Terminal instance.
    static Terminal* instance() { return instance_; }

    /// @brief Maximum length of the line-input buffer.
    static constexpr size_t LINE_BUF_SIZE = 256;

private:
    static Terminal* instance_;

    static constexpr uint32_t DEFAULT_FG = 0xC0C0C0;
    static constexpr uint32_t DEFAULT_BG = 0x000000;

    uint32_t cursor_x_ = 0;
    uint32_t cursor_y_ = 0;
    uint32_t fg_ = DEFAULT_FG;
    uint32_t bg_ = DEFAULT_BG;
    uint32_t cols_ = 0;
    uint32_t rows_ = 0;

    char line_buf_[LINE_BUF_SIZE];
    size_t line_pos_ = 0;

    Terminal() = default;

    /// @brief Moves cursor to the beginning of the next line (scrolls if needed).
    void newline();
    /// @brief Advances the cursor by one column (wraps to next line if needed).
    void advance_cursor();
    /// @brief Moves the cursor back one position (for backspace handling).
    void backspace();
};

} // namespace service
