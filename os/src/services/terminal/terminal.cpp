#include <services/terminal/terminal.hpp>
#include <services/terminal/framebuffer.hpp>
#include <services/terminal/font.hpp>
#include <kernel/arch/io.hpp>
#include <string.hpp>
#include <constants.hpp>

namespace service {

Terminal* Terminal::instance_ = nullptr;

void Terminal::init() {
    if (!Framebuffer::available()) return;

    instance_ = new Terminal{};
    instance_->cols_ = Framebuffer::width() / FONT_WIDTH;
    instance_->rows_ = Framebuffer::height() / FONT_HEIGHT;
    instance_->cursor_x_ = 0;
    instance_->cursor_y_ = 0;
    instance_->fg_ = DEFAULT_FG;
    instance_->bg_ = DEFAULT_BG;
    instance_->line_pos_ = 0;

    clear();
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
    if (instance_->fb_enabled_)
        Framebuffer::clear(instance_->bg_);
    instance_->cursor_x_ = 0;
    instance_->cursor_y_ = 0;
}

void Terminal::scroll() {
    if (!instance_) return;

    uint32_t fb_h = Framebuffer::height();
    uint32_t pitch = Framebuffer::pitch();

    auto* fb = reinterpret_cast<uint8_t*>(
        static_cast<uint64_t>(Framebuffer::info().addr));

    size_t row_size = pitch * FONT_HEIGHT;
    size_t total_scroll = fb_h - FONT_HEIGHT;

    uint8_t* dst = fb;
    uint8_t* src = fb + row_size;
    size_t copy_size = pitch * total_scroll;

    memcpy(dst, src, copy_size);

    uint32_t clear_start = fb_h - FONT_HEIGHT;
    for (uint32_t y = clear_start; y < fb_h; ++y) {
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
}

} // namespace service
