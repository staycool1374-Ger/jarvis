#include <services/terminal/terminal.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/font.hpp>
#include <kernel/arch/io.hpp>
#include <version.hpp>
#include <string.hpp>
#include <constants.hpp>

namespace service {

Terminal* Terminal::instance_ = nullptr;

void Terminal::init() {
    if (!Framebuffer::available()) return;

    Framebuffer::clear(0);
    instance_ = new Terminal{};
    instance_->cols_ = Framebuffer::width() / FONT_WIDTH;
    instance_->rows_ = (Framebuffer::height() / FONT_HEIGHT) - STATUS_BAR_ROWS;
    instance_->cursor_x_ = 0;
    instance_->cursor_y_ = 0;
    instance_->fg_ = DEFAULT_FG;
    instance_->bg_ = DEFAULT_BG;
    instance_->line_pos_ = 0;

#ifdef CONFIG_DEBUG
    draw_status_bar("Jarvis RTOS " KERNEL_VERSION_STRING " [DEBUG]", "");
#else
    draw_status_bar("Jarvis RTOS " KERNEL_VERSION_STRING " [RELEASE]", "");
#endif
}

static void serial_putchar(char c) {
    if (c == '\n') {
        while ((arch::inb(arch::COM1_LSR) & 0x20) == 0);
        arch::outb(arch::COM1, '\r');
    }
    while ((arch::inb(arch::COM1_LSR) & 0x20) == 0);
    arch::outb(arch::COM1, c);
}

void Terminal::putchar(char c) {
    serial_putchar(c);

    // Capture output for shell redirection
    if (instance_ && instance_->capture_buf_ &&
        instance_->capture_pos_ < instance_->capture_size_ - 1) {
        instance_->capture_buf_[instance_->capture_pos_++] = c;
        instance_->capture_buf_[instance_->capture_pos_] = '\0';
    }

    if (!instance_) return;
    if (!instance_->fb_enabled_) return;

    switch (c) {
    case '\n':
        instance_->newline();
        break;
    case '\r':
        instance_->cursor_x_ = 0;
        break;
    case '\t':
        for (int i = 0; i < 4; ++i) putchar(' ');
        break;
    case '\b':
        instance_->backspace();
        break;
    default:
        if (c >= 32 && c <= 126) {
            Framebuffer::draw_char(
                instance_->cursor_x_ * FONT_WIDTH,
                instance_->cursor_y_ * FONT_HEIGHT,
                c, instance_->fg_, instance_->bg_);
            instance_->advance_cursor();
        }
        break;
    }
}

void Terminal::write(const char* str) {
    while (*str) putchar(*str++);
}

void Terminal::write(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) putchar(data[i]);
}

void Terminal::puts(const char* str) {
    write(str);
    putchar('\n');
}

bool Terminal::readline(char* buf, size_t max_len) {
    if (!instance_) return false;

    instance_->line_pos_ = 0;

    while (true) {
        while (!(arch::inb(arch::COM1_LSR) & 1)) {
            arch::pause();
        }
        char c = arch::inb(arch::COM1);
        if (c == '\r') c = '\n';

        if (c == '\n') {
            putchar('\n');
            buf[instance_->line_pos_] = '\0';
            instance_->line_pos_ = 0;
            return true;
        }

        if (c == '\b' || c == 0x7F) {
            if (instance_->line_pos_ > 0) {
                --instance_->line_pos_;
                putchar('\b');
            }
            continue;
        }

        if (instance_->line_pos_ < max_len - 1) {
            buf[instance_->line_pos_++] = c;
            putchar(c);
        }
    }
}

void Terminal::set_fg(uint32_t color) {
    if (instance_) instance_->fg_ = color;
}

void Terminal::set_bg(uint32_t color) {
    if (instance_) instance_->bg_ = color;
}

void Terminal::clear() {
    if (!instance_) {
        Framebuffer::clear(0);
        return;
    }
    if (!instance_->fb_enabled_) return;
    uint32_t pitch = Framebuffer::pitch();
    uint32_t text_h = instance_->rows_ * FONT_HEIGHT;
    auto* fb = reinterpret_cast<uint8_t*>(
        static_cast<uint64_t>(Framebuffer::info().addr));
    memset(fb, 0, pitch * text_h);
    instance_->cursor_x_ = 0;
    instance_->cursor_y_ = 0;
}

void Terminal::scroll() {
    if (!instance_) return;

    uint32_t pitch = Framebuffer::pitch();
    auto* fb = reinterpret_cast<uint8_t*>(
        static_cast<uint64_t>(Framebuffer::info().addr));

    size_t row_size = pitch * FONT_HEIGHT;
    uint32_t text_h = instance_->rows_ * FONT_HEIGHT;

    memcpy(fb, fb + row_size, pitch * (text_h - FONT_HEIGHT));

    uint32_t clear_start = text_h - FONT_HEIGHT;
    for (uint32_t y = clear_start; y < text_h; ++y) {
        size_t off = y * pitch;
        memset(fb + off, 0, pitch);
    }

    if (instance_->cursor_y_ > 0) {
        instance_->cursor_y_--;
    }
}

void Terminal::newline() {
    cursor_x_ = 0;
    if (++cursor_y_ >= rows_) {
        scroll();
    }
}

void Terminal::advance_cursor() {
    if (++cursor_x_ >= cols_) {
        newline();
    }
}

void Terminal::backspace() {
    uint32_t old_x = cursor_x_;
    uint32_t old_y = cursor_y_;

    if (cursor_x_ > 0) {
        --cursor_x_;
    } else if (cursor_y_ > 0) {
        --cursor_y_;
        cursor_x_ = cols_ - 1;
    }

    Framebuffer::draw_char(
        cursor_x_ * FONT_WIDTH,
        cursor_y_ * FONT_HEIGHT,
        ' ', fg_, bg_);

    if (old_x != cursor_x_ || old_y != cursor_y_) {
        Framebuffer::draw_char(
            old_x * FONT_WIDTH,
            old_y * FONT_HEIGHT,
            ' ', fg_, bg_);
    }
}

void Terminal::draw_status_bar(const char* left, const char* right) {
    if (!instance_ || !instance_->fb_enabled_) return;
    if (!Framebuffer::available()) return;

    uint32_t fb_w = Framebuffer::width();
    uint32_t sb_y = instance_->rows_ * FONT_HEIGHT;
    uint32_t sb_h = STATUS_BAR_ROWS * FONT_HEIGHT;

    Framebuffer::fill_rect(0, sb_y, fb_w, sb_h, 0x0D1117);
    Framebuffer::fill_rect(0, sb_y, fb_w, 2, 0x30363D);

    uint32_t text_y = sb_y + FONT_HEIGHT;

    uint32_t x = 2 * FONT_WIDTH;
    for (const char* p = left; *p; ++p) {
        Framebuffer::draw_char(x, text_y, *p, 0xC0C0C0, 0x0D1117);
        x += FONT_WIDTH;
    }

    size_t right_len = 0;
    const char* rp = right;
    while (*rp++) ++right_len;

    if (right_len > 0) {
        uint32_t right_x = fb_w - (right_len + 2) * FONT_WIDTH;
        for (const char* p = right; *p; ++p) {
            Framebuffer::draw_char(right_x, text_y, *p, 0x00AAFF, 0x0D1117);
            right_x += FONT_WIDTH;
        }
    }
}

void Terminal::set_cursor_visible(bool visible) {
    if (!instance_ || !instance_->fb_enabled_) return;
    if (!Framebuffer::available()) return;
    uint32_t color = visible ? 0xC0C0C0 : 0x000000;
    Framebuffer::fill_rect(
        instance_->cursor_x_ * FONT_WIDTH,
        instance_->cursor_y_ * FONT_HEIGHT,
        FONT_WIDTH, FONT_HEIGHT, color);
}

void Terminal::capture_begin(char* buf, size_t size) {
    if (!instance_) return;
    instance_->capture_buf_ = buf;
    instance_->capture_size_ = size;
    instance_->capture_pos_ = 0;
}

void Terminal::capture_end() {
    if (!instance_) return;
    instance_->capture_buf_ = nullptr;
    instance_->capture_size_ = 0;
    instance_->capture_pos_ = 0;
}

void Terminal::show_splash() {
    if (!instance_ || !instance_->fb_enabled_) return;
    if (!Framebuffer::available()) return;

    Framebuffer::clear(0x0A0A10);

    uint32_t fb_w = Framebuffer::width();
    uint32_t fb_h = Framebuffer::height();
    uint32_t total_rows = fb_h / FONT_HEIGHT;

    const char* title = "Jarvis RTOS";
    size_t title_len = 0;
    for (const char* p = title; *p; ++p) ++title_len;
    uint32_t title_x = (fb_w - title_len * FONT_WIDTH) / 2;
    uint32_t title_y = (total_rows / 2 - 2) * FONT_HEIGHT;
    for (const char* p = title; *p; ++p) {
        Framebuffer::draw_char(title_x, title_y, *p, 0x00FF00, 0x0A0A10);
        title_x += FONT_WIDTH;
    }

    // Version string in main color
    uint32_t ver_color = 0x00FF00;
    {
        char ver_line[48];
        int pos = 0;
        for (const char* p = KERNEL_VERSION_STRING; *p; ++p) ver_line[pos++] = *p;
        ver_line[pos] = '\0';
        uint32_t ver_x = (fb_w - pos * FONT_WIDTH) / 2;
        uint32_t ver_y = (total_rows / 2) * FONT_HEIGHT;
        for (int i = 0; i < pos; ++i) {
            Framebuffer::draw_char(ver_x, ver_y, ver_line[i], ver_color, 0x0A0A10);
            ver_x += FONT_WIDTH;
        }
    }

    // Build type tag in a distinguishing color
    {
#ifdef CONFIG_DEBUG
        const char* tag = "[DEBUG]";
        uint32_t tag_color = 0xFFFF00;
#else
        const char* tag = "[RELEASE]";
        uint32_t tag_color = 0x00FF00;
#endif
        size_t tag_len = 0;
        for (const char* p = tag; *p; ++p) ++tag_len;
        uint32_t tag_x = (fb_w - tag_len * FONT_WIDTH) / 2;
        uint32_t tag_y = (total_rows / 2 + 1) * FONT_HEIGHT;
        for (const char* p = tag; *p; ++p) {
            Framebuffer::draw_char(tag_x, tag_y, *p, tag_color, 0x0A0A10);
            tag_x += FONT_WIDTH;
        }
    }

    const char* hint = "Type 'help' for available commands";
    size_t hint_len = 0;
    for (const char* p = hint; *p; ++p) ++hint_len;
    uint32_t hint_x = (fb_w - hint_len * FONT_WIDTH) / 2;
    uint32_t hint_y = (total_rows / 2 + 2) * FONT_HEIGHT;
    for (const char* p = hint; *p; ++p) {
        Framebuffer::draw_char(hint_x, hint_y, *p, 0x606060, 0x0A0A10);
        hint_x += FONT_WIDTH;
    }
}

} // namespace service
