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

#include <kernel/random.hpp>
#include <kernel/arch/cpuid.hpp>
#include <kernel/arch/rand.hpp>
#include <kernel/arch/io.hpp>
#include <chacha.hpp>
#include <string.hpp>

namespace kernel {

using namespace crypto;

// ── CSPRNG state ──

namespace {

struct PrngState {
    uint8_t  key[CHACHA_KEY_SIZE];
    uint8_t  nonce[CHACHA_NONCE_SIZE];
    uint32_t counter;
    uint8_t  buffer[CHACHA_BLOCK_SIZE];
    size_t   position;
    uint64_t blocks_generated;
    bool     have_rdrand;
    bool     have_rdseed;
};

static PrngState g_state;
static bool g_initialized = false;

} // anonymous namespace

// ── Seed helpers ──

/// @brief Fill a buffer with random bytes from RDSEED (preferred) or RDRAND.
/// @return true if at least one good random value was obtained.
static bool seed_from_hwrng(uint8_t* buf, size_t len) {
    while (len >= sizeof(uint64_t)) {
        uint64_t val;
        bool ok = false;
        if (g_state.have_rdseed) {
            // RDSEED may fail occasionally; retry a few times
            for (int retry = 0; retry < 10; ++retry) {
                if (arch::rdseed64(val)) { ok = true; break; }
            }
        } else if (g_state.have_rdrand) {
            for (int retry = 0; retry < 10; ++retry) {
                if (arch::rdrand64(val)) { ok = true; break; }
            }
        }
        if (!ok) return false;
        memcpy(buf, &val, sizeof(uint64_t));
        buf += sizeof(uint64_t);
        len -= sizeof(uint64_t);
    }
    if (len > 0) {
        uint64_t val;
        bool ok = false;
        if (g_state.have_rdseed) {
            for (int retry = 0; retry < 10; ++retry) {
                if (arch::rdseed64(val)) { ok = true; break; }
            }
        } else if (g_state.have_rdrand) {
            for (int retry = 0; retry < 10; ++retry) {
                if (arch::rdrand64(val)) { ok = true; break; }
            }
        }
        if (!ok) return false;
        memcpy(buf, &val, len);
    }
    return true;
}

/// @brief Fallback: derive seed material from RDTSC jitter.
static void seed_from_jitter(uint8_t* key, uint8_t* nonce) {
    uint64_t seed[8] = {0};
    for (int i = 0; i < 100; ++i) {
        seed[i & 7] ^= arch::rdtsc();
        for (int j = 0; j < (i + 1) * 10; ++j) {
            asm volatile("");
        }
    }
    memcpy(key, seed, CHACHA_KEY_SIZE);
    memcpy(nonce, reinterpret_cast<uint8_t*>(&seed[4]), CHACHA_NONCE_SIZE);
}

// ── Core PRNG ──

/// @brief Advance the CSPRNG by one ChaCha20 block.
static void prng_next_block() {
    chacha20_block(g_state.key, g_state.counter, g_state.nonce,
        g_state.buffer);
    ++g_state.counter;
    ++g_state.blocks_generated;
    g_state.position = 0;

    // Reseed after 2^20 blocks (~64 MiB of output)
    if ((g_state.blocks_generated & 0xFFFFF) == 0) {
        if (g_state.have_rdrand || g_state.have_rdseed) {
            seed_from_hwrng(g_state.key, CHACHA_KEY_SIZE);
        }
    }
}

// ── Public API ──

void random_init() {
    g_state.have_rdrand = arch::has_rdrand();
    g_state.have_rdseed = arch::has_rdseed();
    g_state.counter = 0;
    g_state.position = CHACHA_BLOCK_SIZE;
    g_state.blocks_generated = 0;

    bool seeded = false;
    if (g_state.have_rdseed || g_state.have_rdrand) {
        seeded = seed_from_hwrng(g_state.key, CHACHA_KEY_SIZE)
              && seed_from_hwrng(g_state.nonce, CHACHA_NONCE_SIZE);
    }

    if (!seeded) {
        seed_from_jitter(g_state.key, g_state.nonce);
    }

    prng_next_block();

    g_initialized = true;
}

void random_fill(uint8_t* buffer, size_t length) {
    if (!g_initialized) {
        // Fill with zeros if not initialized (should not happen)
        memset(buffer, 0, length);
        return;
    }

    while (length > 0) {
        // Generate a new block if current one is exhausted
        if (g_state.position >= CHACHA_BLOCK_SIZE) {
            prng_next_block();
        }

        size_t available = CHACHA_BLOCK_SIZE - g_state.position;
        size_t to_copy = (length < available) ? length : available;

        memcpy(buffer, g_state.buffer + g_state.position, to_copy);
        g_state.position += to_copy;
        buffer += to_copy;
        length -= to_copy;
    }
}

} // namespace kernel
