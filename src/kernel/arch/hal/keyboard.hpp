#pragma once

#include <types.hpp>
#include <kernel/sync/spsc_ring.hpp>

namespace arch {

class Keyboard {
public:
    Keyboard() = default;
    static void init();
    static void handle_irq();
    static bool getchar(char& c);
    static size_t read(char* buf, size_t len);
    static void flush();
    static bool is_shifted() { return shift_; }
    static bool is_ctrl() { return ctrl_; }
    static bool is_alt() { return alt_; }

private:
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint16_t DATA_PORT = 0x60;
    static constexpr uint16_t STATUS_PORT = 0x64;
    static SPSCRing<char, RING_SIZE> ring_;
    static bool shift_;
    static bool ctrl_;
    static bool alt_;
    static bool caps_;
    static bool push_ring(char c);
    static void update_modifiers(uint8_t scancode, bool pressed);
};

} // namespace arch
