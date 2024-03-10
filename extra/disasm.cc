#include "asmjit/src/asmjit/asmjit.h"

using namespace asmjit;

#include <Zydis/Zydis.h>

void dump_disasm(const CodeHolder& code) {
    auto buffer = code.textSection()->buffer();
    uint8_t* data = buffer.data();
    size_t size = buffer.size();
    ZyanU64 runtime_address = 0;
    ZyanUSize offset = 0;
    ZydisDisassembledInstruction instruction;
    while (ZYAN_SUCCESS(ZydisDisassembleIntel(
        ZYDIS_MACHINE_MODE_LONG_64,
        runtime_address,
        (char*)data + offset, size - offset,
        &instruction
    ))) {
        printf("%s\n", instruction.text);
        offset += instruction.info.length;
        runtime_address += instruction.info.length;
    }
    printf("(%lu bytes)\n", size);
}
