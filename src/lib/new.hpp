#pragma once

/// @brief Placement new — required for `new (ptr) Type(...)` syntax.
/// These do NOT allocate; they return the pointer unchanged.
void* operator new(unsigned long size, void* ptr) noexcept;
void* operator new[](unsigned long size, void* ptr) noexcept;
