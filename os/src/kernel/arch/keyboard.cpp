#include <kernel/arch/keyboard.hpp>
#include <kernel/arch/io.hpp>

namespace arch {

volatile char Keyboard::ring_[RING_SIZE] = {};
volatile size_t Keyboard::head_ = 0;
volatile size_t Keyboard::tail_ = 0;
bool Keyboard::shift_ = false;
bool Keyboard::ctrl_ = false;
bool Keyboard::alt_ = false;
bool Keyboard::caps_ = false;

static const char scancode_lower[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,   'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`', 0,   '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static const char scancode_upper[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,   'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

void Keyboard::init() {
    head_ = 0;
    tail_ = 0;
    shift_ = false;
    ctrl_ = false;
    alt_ = false;

    outb(0x64, 0xAE);
    outb(0x60, 0xF4);
    io_wait();
}

bool Keyboard::push_ring(char c) {
    size_t next = (head_ + 1) % RING_SIZE;
    if (next == tail_) return false;
    ring_[head_] = c;
    head_ = next;
    return true;
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
    if (tail_ == head_) return false;
    c = ring_[tail_];
    tail_ = (tail_ + 1) % RING_SIZE;
    return true;
}

size_t Keyboard::read(char* buf, size_t len) {
    size_t count = 0;
    while (count < len && tail_ != head_) {
        buf[count++] = ring_[tail_];
        tail_ = (tail_ + 1) % RING_SIZE;
    }
    return count;
}

void Keyboard::flush() {
    head_ = 0;
    tail_ = 0;
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
