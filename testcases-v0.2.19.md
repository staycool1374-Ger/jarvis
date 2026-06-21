# Test Cases — v0.2.19 (System Calls & Storage)

### DMA Driver — test_dma.cpp
- DMA completion interrupt fires and is acknowledged — DMA transfer completes, ISR fires, driver acknowledges and clears pending status
- Double-buffered transfer — Two alternating DMA buffers used in ping-pong mode; back-to-back transfers complete without data corruption
