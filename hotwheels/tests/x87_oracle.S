/* Same instruction bytes run by Wine x86 and libwinx86 under qemu-arm.
 * One guest invocation preserves sticky status across all conversions. */
.text
.globl _x87_fistp_case
.def _x87_fistp_case; .scl 2; .type 32; .endef
_x87_fistp_case:
    .byte 0xeb, 0x00                 /* short jump-to-next, for cache source-mismatch test */
    pushl %esi
    pushl %edi
    subl $4, %esp
    fnstcw (%esp)
    fninit
    movl 16(%esp), %esi
    movl 20(%esp), %edi
.macro CASE off, next
    fldcw (\off+8)(%esi)
    fldl (\off)(%esi)
    fistpll (\off)(%edi)
    fnstsw (\off+8)(%edi)
    jmp \next
.endm
    CASE 0, .Lcase1
.Lcase1: CASE 16, .Lcase2
.Lcase2: CASE 32, .Lcase3
.Lcase3: CASE 48, .Lcase4
.Lcase4: CASE 64, .Lcase5
.Lcase5: CASE 80, .Lcase6
.Lcase6: CASE 96, .Lcase7
.Lcase7: CASE 112, .Lcase8
.Lcase8: CASE 128, .Lcase9
.Lcase9: CASE 144, .Lcase10
.Lcase10: CASE 160, .Lcase11
.Lcase11: CASE 176, .Ldone
.Ldone:
    fninit
    fildll 192(%esi)
    fistpll 192(%edi)
    fnstsw 200(%edi)
    fldcw (%esp)
    addl $4, %esp
    popl %edi
    popl %esi
    ret
