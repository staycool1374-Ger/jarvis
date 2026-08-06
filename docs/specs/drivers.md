# Hardware Driver Layer & Interrupt Architecture Specification

**Semantics:** binding contracts for the block-device abstraction, the AHCI /
ATA-PIO / virtio-blk drivers, the DMA engine, and the x86_64 interrupt
dispatch.  Includes the confirmed audit flaws (`audits/hardware_ahci.md`) that
a spec MUST state as open hardening requirements.  All symbols verified against
the current tree.

## 1. Block-Device Abstraction

```cpp
class kernel::block::BlockDevice {          // pure virtual, BLOCK_SIZE=512
    virtual bool read_sector(uint64_t lba, uint8_t *buffer);   // false on any failure
    virtual bool write_sector(uint64_t lba, const uint8_t *buffer);
    virtual uint64_t sector_count() const;
    virtual uint64_t sector_size() const;   // 512
    virtual bool is_read_only() const;
};
```
`AhciDriver`, `AtaPioDriver`, `VirtioBlkDriver` implement it — synchronous,
blocking, single-command-at-a-time from the caller's perspective.  There is no
multi-request queue at the BlockDevice layer.  Drivers use static `probe()`
factories (`MemPool::alloc` + placement-new), tracked by ResourceTracker.

## 2. DMA Contract

### 2.1 Buffer & scatter-gather
- `DmaBuffer {phys_addr, virt_addr, size, owned}` — `alloc_buffer()` =
  PMM contiguous + VMM map at HHDM + zero; `free_buffer()` = unmap + free.
- `SgList` (≤256 entries), `PrdTable` (≤256 `PrdEntry`: ATA bus-master format,
  `byte_count = count-1`, bit15 = EOT).  `DmaChannel` = `init/start/is_busy/
  handle_irq/abort`; `BmDmaChannel` via BMDMA PCI I/O ports.

### 2.2 DmaEngine state machine
```
start_transfer(prd, dir, cb, ctx):  if active_ → false; channel_.start; active_=true;
                                    callback_=cb; callback_ctx_=ctx        // NOT atomic
handle_irq() (ISR ctx):             if !active_ → false; success = channel_.handle_irq();
                                    active_=false; if cb → cb(ctx, success)  // cb IN IRQ
is_busy():  active_ && channel_.is_busy() (clears active_ if HW idle)
abort():    channel_.abort(); active_=false; callback_=nullptr
```
**FLAW-01 (confirmed, OPEN):** `active_`/`callback_`/`callback_ctx_` are
mutated by `handle_irq()` (ISR) and by `start_transfer()`/`abort()`/`is_busy()`
(task) with **zero mutual exclusion** — a data race.  **Required:** a
`sync::SpinLock` member acquired via `IrqSpinLockGuard` (cli + lock — same core
runs the ISR, so a plain SpinLock self-deadlocks); in `handle_irq()` capture
`callback_`/`callback_ctx_` into stack locals while holding the lock, release,
then invoke the callback **outside** the critical section.

**FLAW-02 (confirmed, OPEN):** `PingPongDma` shares `prepare_idx_`/`xfer_idx_`/
`active_`/`completed_`/`chain_cb_`/`chain_ctx_` across task/IRQ with no lock —
an in-flight DMA target can be handed to the producer.  Same fix pattern;
`prepare_buf`/`xfer_buf` return the resolved pointer while holding the lock.

## 3. AHCI (`ahci.cpp`)

- **Init:** PCI find (class 01/06) → ABAR = BAR5 (validate `bar_count>5`,
  `address!=0`) → map ABAR MMIO page-by-page → set bus master → read
  `HBA_CAP`/`HBA_PI` → HBA reset (GHC_HR poll ≤ 10000) → enable
  `GHC_AE|GHC_IE`.  ⚠️ **FLAW-04:** `GHC_IE` is asserted with **no ISR
  registered**; only the polling `wait_cmd` acknowledges PORT_IS/HBA_IS.
- **Port init:** SSTS DET==3 (online); stop DMA (clear CMD_ST/FRE, wait
  CMD_CR/FR); clear SERR/IS; allocate CL (1 page), RFIS (1 page), CT[32]
  (2 pages each), data buffers[32]; program PORT_CLB/FB; start FRE|ST.
- **Command slot:** `alloc_slot()` = first clear bit in `PORT_CI|PORT_SACT`
  (no per-slot lock — ⚠️ FLAW-04).  `start_cmd()`: zero CT+CH, build CmdFIS
  (type 0x27, PM port 0x80|(tag<<3) for NCQ, `device=0xE0`), 48-bit LBA,
  PRD[0] `(512-1)|IOC`, CmdHeader `cfl=5`, `atomic_fence()`, issue
  `PORT_CI = 1<<slot`.  Commands: READ_DMA_EXT 0x25 / WRITE_DMA_EXT 0x35 /
  READ/WRITE_FPDMA_QUEUED 0x60/0x61 / IDENTIFY 0xEC.
- **`wait_cmd` error paths:** poll PORT_CI clear (≤ 5,000,000 × io_wait);
  PORT_IS TFES → ack+false; TFD_ERR → clear+false; timeout → clear+false.
  ⚠️ **FLAW-05 (OPEN):** an up-to-5-second busy spin that blocks the core and
  starves equal/lower-priority tasks.  **Required:** per-slot completion
  records + a real ISR + scheduler wake; `wait_cmd` becomes a bounded
  blocked-wait.  ⚠️ **FLAW-04 teardown:** `~AhciDriver()` frees CL/RFIS/CT/
  buffers with GHC_IE still enabled → in-flight completion ISR UAF.

## 4. ATA-PIO (`ata_pio.cpp`)

- Register map (base + N): `+0` data, `+2` sector count, `+3..+5` LBA,
  `+6` drive/head (master 0xE0 / slave 0xF0), `+7` status/command.
- `identify()`: select, zero regs, `ATA_CMD_IDENTIFY` (0xEC), reject status
  0/0xFF, `poll_status` BSY-clear, reject ERR, `wait_for_drq`, read 256 words,
  parse `sector_count` (words 60/61 or 100-103).
- `read_sector`/`write_sector`: poll BSY → program LBA → `0x20`/`0x30` →
  `wait_for_drq` → 256× inw/outw → poll status.
- **Pure polling, no IRQ/DMA;** `ATA_POLL_TIMEOUT=100000` io_wait loops.
  Legacy fallback only (not the primary boot IO path in the current tree).

## 5. Virtio-blk (`virtio_blk.cpp`)

- `probe()`: `virtio_find_device(0x1042)` → `init()`: status
  RESET→ACK→DRIVER → negotiate `VIRTIO_F_VERSION_1` → queue_size=16 → 4 PMM
  pages (desc/avail/used/dma_buf) → `virtio_setup_queue` → DRIVER_OK →
  `sector_count` from device_cfg.
- **Descriptor chain** (idx = avail_idx % 16):
  `[idx]  hdr(16, F_NEXT) → [(idx+1)%16] data(512, F_NEXT [+F_WRITE for read])
  → [(idx+2)%16] status(1, F_WRITE)`.
  Ordering: write header/data, `avail->ring[idx]=idx`, fence, `avail->idx++`,
  fence, `virtio_notify` kick.  Completion: busy-poll `used->idx` (≤ 1M),
  status == `VIRTIO_BLK_S_OK`, memcpy out.
  ⚠️ **FLAW-06 (OPEN):** the 1M-iteration busy-wait ignores the DmaEngine
  completion mechanism — unbounded core occupancy.  **Required:** ISR walking
  the used_ ring + wait primitive.

## 6. Interrupt Layer (x86_64)

```
 CPU interrupt gate (IDT[i], type 0x8E; IST1 for #DF; SYSCALL 0x80 trap 0xEE)
   │ vector i (+ error code for ISR_ERR)
   ▼
 isr_common (isr_stubs.asm)
   │ inc [isr_nesting_depth]          ← depth ≤ 2 contract
   │ rdtsc → irq_entry_tsc; push GPRs; rdi=vec rsi=err rdx=rip rcx=rsp
   ▼
 handle_interrupt_c(vec, err, rip, regs, tsc)        [kernel.cpp]
   ├─ v==7        lazy FPU/SSE (fpu_owner, fxsave/fxrstor)
   ├─ user-recover g_user_access_recover_ip redirect
   ├─ v<32        user: deliver_signal / kernel: guard-page check → panic
   ├─ v==0x80     syscall_handler → signals → reschedule
   ├─ THREADED_IRQS: IrqThread::for_vector(v) → isr_entry (ack+Notify) → task
   ├─ else        IDT::handle_interrupt → handlers_[v]
   │               ├─ v==64 timer → Timer::handle_irq → Scheduler::on_tick → re-arm
   │               └─ v==33 kbd   → Keyboard::handle_irq (byte-atomic mods_, SPSC ring)
   ▼
 EOI (APIC for all; PIC for 32–47) + latency histogram
   ▼
 isr_common epilogue:
   cli; deferred-switch? (generation check, depth ≤ 2, stack-bounds check) → switch
   pop GPRs; add rsp,16; dec [isr_nesting_depth]; iretq
```

### 6.1 IRQ allocation
- Static vector map: IRQ0→32, IRQ1→33, IRQ2-15→32+i (masked); APIC timer=64;
  keyboard=33.  No runtime allocator in the IRQ path; `irq_alloc` is enforced
  as a test class (no allocations in timer/keyboard/syscall ISRs).

### 6.2 APIC timer
- TSC-deadline mode when supported (`LVT_TIMER_TSCDEADLINE` + `wrmsr(
  MSR_TSC_DEADLINE)`); else periodic bus-clock (calibrate, INITCNT).  Masks
  I/O APIC IRQ0 (PIT).  Registered handler: `Timer::handle_irq`
  (atomic `ticks_++`) + `Scheduler::on_tick()` + re-arm.

### 6.3 Nesting depth (≤ 2)
- Depth 1 = normal IRQ; 2 = SYSCALL+timer nesting; ≥ 3 = detected bug — the
  context switch in the epilogue is skipped.  `on_tick` checks it to skip
  re-entrant scheduler ops.  Tests reset it to 0.

### 6.4 Keyboard
- Vector 33; threaded mode = `IrqThread::create(33, prio 50, ...)`.
- `handle_irq` reads STATUS/DATA, updates `mods_` byte-atomic (valid lock-free
  SPSC producer pattern — audit V-5 dismissed), translates scancode → ASCII,
  `push_ring`.  ⚠️ **FLAW-10 (OPEN):** `init()` has an **unbounded** PS/2
  output-buffer drain loop — must be capped like the second bounded drain.

## 7. Binding Invariants

1. **No dynamic allocation in the IRQ path** (MemPool/PMM/heap) — completion
   state is statically embedded; enforced by the `irq_alloc` test class.
2. **Bounded blocking everywhere.** AHCI `wait_cmd` (5M spins, FLAW-05),
   virtio `submit_request` (1M spins, FLAW-06), serial (FLAW-08), keyboard
   drain (FLAW-10) must become bounded loops or scheduler-blocked waits.
   Timeout values are the *blocked-wait bound*, not a spin bound.
3. **Spinlock-in-IRQ rules.** Shared ISR/task state (DmaEngine, PingPongDma,
   AHCI port command state) needs a `SpinLock` via `IrqSpinLockGuard`;
   callbacks invoked only after release, from stack-captured locals; no
   allocation/blocking while holding a lock in IRQ context.
4. **`GHC_IE` ordering.** AHCI global interrupt-enable must not be set until a
   real ISR is wired (FLAW-04); teardown clears GHC_IE/PORT_IE and takes port
   locks before freeing CL/RFIS/CT/data memory.
5. **UART FIFO drain.** 16550 FIFO = 16 bytes; `Serial::putchar` polls THRE per
   char and must drain between bursts to avoid overflow (perturbs timing).
6. **Memory ordering for DMA.** `kernel::atomic_fence()` precedes issuing
   commands (AHCI `PORT_CI` write, virtio avail idx increment + kick) so
   descriptor writes are visible before the doorbell.
7. **Nesting depth ≤ 2**; deeper = corruption (switch skipped).

## 8. Open Flaw Ledger

| Flaw | Location | Status |
|---|---|---|
| FLAW-01 DmaEngine ISR/task race | dma.cpp | OPEN — spec required (§2.2) |
| FLAW-02 PingPongDma index race | dma.cpp | OPEN (§2.2) |
| FLAW-03 virtio-net ring races | virtio_net.cpp | OPEN (same class) |
| FLAW-04 AHCI GHC_IE w/o ISR + teardown UAF | ahci.cpp | OPEN (§3) |
| FLAW-05 AHCI 5s busy-poll | ahci.cpp wait_cmd | OPEN (§3) |
| FLAW-06 virtio-blk 1M spin | virtio_blk.cpp | OPEN (§5) |
| FLAW-08 serial unbounded polling | serial.cpp | OPEN (§7.2) |
| FLAW-10 keyboard unbounded drain | keyboard.cpp | OPEN (§6.4) |
