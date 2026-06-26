// Early init is done in boot.S (assembly).
// This file exists only to keep the build system from complaining
// about a missing .cpp file in the riscv64 arch directory.
struct EarlyInitStub {
    EarlyInitStub() {}
};
static EarlyInitStub stub;
