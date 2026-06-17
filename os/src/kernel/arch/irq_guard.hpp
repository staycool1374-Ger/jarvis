#pragma once

#include <kernel/arch/io.hpp>

namespace arch {

class [[nodiscard]] IrqGuard {
public:
    IrqGuard() noexcept
        : irq_was_(interrupts_enabled())
    {
        cli();
    }

    ~IrqGuard() noexcept
    {
        if (irq_was_) sti();
    }

    IrqGuard(const IrqGuard&)            = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;
    IrqGuard(IrqGuard&&)                 = delete;
    IrqGuard& operator=(IrqGuard&&)      = delete;

private:
    bool irq_was_;
};

} // namespace arch
