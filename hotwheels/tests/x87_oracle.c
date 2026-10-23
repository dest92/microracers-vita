#include <stdint.h>
#include <stdio.h>

struct result { uint64_t value; uint16_t status; uint16_t pad; };
struct input { uint64_t bits; uint16_t cw; uint16_t pad; uint32_t reserved; };
extern void x87_fistp_case(const struct input *, struct result *);

struct test { const char *name; uint64_t bits; uint16_t cw; };
static const struct test tests[] = {
    {"pos-half-nearest-even", 0x3fe0000000000000ULL, 0x037f},
    {"neg-one-point-five-nearest-even", 0xbff8000000000000ULL, 0x037f},
    {"pos-one-half-down", 0x3fe0000000000000ULL, 0x077f},
    {"neg-one-half-up", 0xbfe0000000000000ULL, 0x0b7f},
    {"trunc-pos-fraction", 0x400e000000000000ULL, 0x0f7f},
    {"trunc-neg-fraction", 0xc00e000000000000ULL, 0x0f7f},
    {"int32-max", 0x41dfffffffc00000ULL, 0x0f7f},
    {"int32-min", 0xc1e0000000000000ULL, 0x0f7f},
    {"int32-overflow", 0x41e0000000000000ULL, 0x0f7f},
    {"small-denormal", 0x0000000000000001ULL, 0x0f7f},
    {"positive-infinity", 0x7ff0000000000000ULL, 0x0f7f},
    {"quiet-nan", 0x7ff8000000000001ULL, 0x0f7f},
    {"exact-fild-fistp", 0x0000000180000001ULL, 0x037f},
};

int main(void) {
    struct input inputs[sizeof tests / sizeof tests[0]];
    struct result outputs[sizeof tests / sizeof tests[0]] = {{0}};
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        inputs[i] = (struct input){tests[i].bits, tests[i].cw, 0, 0};
    }
    x87_fistp_case(inputs, outputs);
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
        printf("%s|%016llx|%04x\n", tests[i].name,
               (unsigned long long)outputs[i].value, outputs[i].status);
    }
    return 0;
}
