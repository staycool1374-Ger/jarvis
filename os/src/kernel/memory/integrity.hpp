#pragma once

#include <types.hpp>

namespace kernel {
namespace integrity {

extern const uint64_t _m_text_start;
extern const uint64_t _m_text_end;
extern const uint64_t _m_rodata_start;
extern const uint64_t _m_rodata_end;
extern const uint64_t _m_data_start;
extern const uint64_t _m_data_end;
extern const uint64_t _m_bss_start;
extern const uint64_t _m_bss_end;
extern const uint64_t _m_stack_before;
extern const uint64_t _m_stack_after;

extern "C" {
    extern uint64_t _text_start[];
    extern uint64_t _text_end[];
    extern uint64_t _expected_code_crc[];
}

bool check_section_markers();
void reset_crc_state();
bool crc_process_chunk();
void idle_task_main();

}
}