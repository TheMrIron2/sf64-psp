    .set noreorder

#include "pspstub.s"

    .section .rodata.sceResident, "a"
    .word 0
.Lsf64PspMeKcallModuleName:
    .asciz "kcall"
    .align 2

    .section .lib.stub, "a", @progbits
    .globl sf64PspMeKcallImport
    .type sf64PspMeKcallImport, @object
sf64PspMeKcallImport:
    .word .Lsf64PspMeKcallModuleName
    .word 0x40090000
    .word 0x00030005
    .word .Lsf64PspMeKcallNids
    .word .Lsf64PspMeKcallStubs
    .size sf64PspMeKcallImport, . - sf64PspMeKcallImport

    .section .rodata.sceNid, "a"
.Lsf64PspMeKcallNids:

    .section .sceStub.text, "ax", @progbits
.Lsf64PspMeKcallStubs:

    STUB_FUNC 0x75F43FF0, kcall_2
    STUB_FUNC 0xBADD8D3B, kcall_3
    STUB_FUNC 0x2BB46CB6, kinit
