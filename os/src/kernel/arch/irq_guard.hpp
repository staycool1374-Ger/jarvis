/*
 * Jarvis RTOS — Development Roadmap / Kernel Core
 * Copyright (C) 2026 Arnold Hasshold
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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
