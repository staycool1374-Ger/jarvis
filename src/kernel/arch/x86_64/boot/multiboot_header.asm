; Jarvis RTOS — Development Roadmap / Kernel Core
; Copyright (C) 2026 Arnold Hasshold
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

section .multiboot_header

MAGIC          equ 0xE85250D6
ARCH           equ 0

multiboot_start:
    dd MAGIC
    dd ARCH
    dd multiboot_end - multiboot_start
    dd 0x100000000 - (MAGIC + ARCH + (multiboot_end - multiboot_start))

    dw 5       ; tag type: framebuffer request
    dw 1       ; flags: optional
    dd 20      ; size
    dd 1024    ; width
    dd 768     ; height
    dd 32      ; depth

    align 8, db 0

    dw 0       ; end tag type
    dw 0       ; end tag flags
    dd 8       ; end tag size
multiboot_end:
