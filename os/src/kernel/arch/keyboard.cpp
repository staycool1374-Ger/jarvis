#include <kernel/arch/keyboard.hpp>
#include <kernel/arch/io.hpp>

namespace arch {

SPSCRing<char, Keyboard::RING_SIZE> Keyboard::ring_;
bool Keyboard::shift_ = false;
bool Keyboard::ctrl_ = false;
bool Keyboard::alt_ = false;
bool Keyboard::caps_ = false;

static const char scancode_lower[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
        0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,   'a',
        's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`', 0,   '\\','z', 'x', 'c',
        'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
};

static const char scancode_upper[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
        0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,   'A',
        'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C',
        'V',
    'B', 'N', 'M', '<', '>', '?', 0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,
};

void Keyboard::init() {
    ring_.reset();
    shift_ = false;
    ctrl_ = false;
    alt_ = false;

    outb(0x64, 0xAE);
    outb(0x60, 0xF4);
    io_wait();
}

bool Keyboard::push_ring(char c) {
    return ring_.try_push(c);
}

void Keyboard::handle_irq() {
    uint8_t status = inb(STATUS_PORT);
    if (!(status & 0x01)) return;

    uint8_t scancode = inb(DATA_PORT);

    bool pressed = !(scancode & 0x80);
    uint8_t code = scancode & 0x7F;

    update_modifiers(scancode, pressed);

    if (!pressed) return;

    if (code == 0x1C) { push_ring('\n'); return; }
    if (code == 0x0E) { push_ring('\b'); return; }
    if (code == 0x0F) { push_ring('\t'); return; }

    bool use_shift = (shift_ && !caps_) || (!shift_ && caps_);
    char c = use_shift ? scancode_upper[code] : scancode_lower[code];
    if (c) push_ring(c);
}

bool Keyboard::getchar(char& c) {
    return ring_.try_pop(c);
}

size_t Keyboard::read(char* buf, size_t len) {
    size_t count = 0;
    while (count < len && ring_.try_pop(buf[count]))
        ++count;
    return count;
}

void Keyboard::flush() {
    ring_.reset();
}

void Keyboard::update_modifiers(uint8_t scancode, bool pressed) {
    uint8_t code = scancode & 0x7F;
    switch (code) {
    case 0x2A: case 0x36: shift_ = pressed; break;
    case 0x1D: ctrl_ = pressed; break;
    case 0x38: alt_ = pressed; break;
    case 0x3A:
        if (pressed) caps_ = !caps_;
        break;
    }
}

} // namespace arch