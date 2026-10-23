#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "x86emu_private.h"

void idiv32(x86emu_t* emu, uint32_t divisor);

int main(void) {
    x86emu_t emu;
    uint32_t parity[8] = {0};
    memset(&emu, 0, sizeof emu);
    emu.x86emu_parity_tab = parity;
    emu.regs[_DX].dword[0] = 0xffffffffu;
    emu.regs[_AX].dword[0] = 0x80000000u;
    idiv32(&emu, 1);
    if (emu.quit || emu.error || emu.regs[_AX].dword[0] != 0x80000000u || emu.regs[_DX].dword[0] != 0) {
        fprintf(stderr, "IDIV -2147483648 / 1: quit=%d error=%d eax=%08x edx=%08x\n",
            emu.quit, emu.error, emu.regs[_AX].dword[0], emu.regs[_DX].dword[0]);
        return 1;
    }
    memset(&emu, 0, sizeof emu);
    emu.x86emu_parity_tab = parity;
    idiv32(&emu, 0);
    if (!emu.quit || emu.error != ERR_DIVBY0) {
        fprintf(stderr, "IDIV / 0 debe disparar #DE: quit=%d error=%d\n", emu.quit, emu.error);
        return 2;
    }
    puts("PASS Box86 idiv32: acepta cociente INT32_MIN y preserva #DE por divisor cero");
    return 0;
}
