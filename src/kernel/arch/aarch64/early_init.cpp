// Early init is now done in boot.S (assembly).
// This file exists only to keep the build system from complaining
// about a missing .cpp file in the aarch64 arch directory.
struct EarlyInitStub {
    EarlyInitStub() {}
};
static EarlyInitStub stub;
