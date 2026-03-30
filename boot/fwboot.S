/*
 * fwboot.S — minimal option ROM
 *
 * Reads kernel_entry from fw_cfg, switches to 32-bit protected mode,
 * and jumps to the kernel. Kernel data must already be in guest RAM.
 */

#define FW_CFG_SEL          0x0510
#define FW_CFG_DATA         0x0511
#define FW_CFG_KERNEL_SIZE  0x0008
#define FW_CFG_KERNEL_ENTRY 0x0010

        .code16
        .section .text

        .globl _start
_start:
        .byte   0x55, 0xAA             /* ROM signature */
        .byte   (end - _start) / 512   /* size in 512B blocks */

/* ---- init: BIOS far-calls here ---- */
init:
        pushal
        pushw   %ds
        pushw   %es
        pushw   %cs
        popw    %ds                     /* DS = CS = ROM segment */

        /* ---- check FW_CFG_KERNEL_SIZE != 0 ---- */
        movw    $FW_CFG_SEL, %dx
        movw    $FW_CFG_KERNEL_SIZE, %ax
        outw    %ax, %dx
        movw    $FW_CFG_DATA, %dx
        inb     %dx, %al
        movb    %al, %cl
        inb     %dx, %al
        orb     %al, %cl
        inb     %dx, %al
        orb     %al, %cl
        inb     %dx, %al
        orb     %al, %cl
        testb   %cl, %cl
        jz      .Lret

        /* ---- read FW_CFG_KERNEL_ENTRY (32-bit LE) into EBX ---- */
        movw    $FW_CFG_SEL, %dx
        movw    $FW_CFG_KERNEL_ENTRY, %ax
        outw    %ax, %dx
        movw    $FW_CFG_DATA, %dx

        inb     %dx, %al
        movzbl  %al, %ebx              /* byte 0 */
        inb     %dx, %al
        movzbl  %al, %ecx
        shll    $8, %ecx
        orl     %ecx, %ebx             /* byte 1 */
        inb     %dx, %al
        movzbl  %al, %ecx
        shll    $16, %ecx
        orl     %ecx, %ebx             /* byte 2 */
        inb     %dx, %al
        movzbl  %al, %ecx
        shll    $24, %ecx
        orl     %ecx, %ebx             /* byte 3 — EBX = entry */

        testl   %ebx, %ebx
        jz      .Lret

        cli

        /* ---- enable A20 ---- */
        inb     $0x92, %al
        orb     $0x02, %al
        andb    $0xfe, %al
        outb    %al, $0x92

        /* ---- patch GDT base = CS*16 + gdt offset ---- */
        xorl    %eax, %eax
        movw    %cs, %ax
        shll    $4, %eax
        addl    $(gdt - _start), %eax
        movl    %eax, (gdtr - _start + 2)

        /* ---- patch far-jump target = CS*16 + .Lpm32 offset ---- */
        xorl    %eax, %eax
        movw    %cs, %ax
        shll    $4, %eax
        addl    $(.Lpm32 - _start), %eax
        movl    %eax, (.Ljmp_target - _start)

        /* ---- load GDT ---- */
        lgdtl   (gdtr - _start)

        /* ---- enter protected mode ---- */
        movl    %cr0, %eax
        orl     $1, %eax
        movl    %eax, %cr0

        /* ---- far jump to 32-bit code ---- */
        .byte   0x66, 0xea             /* ljmpl */
.Ljmp_target:
        .long   0                       /* patched above */
        .word   0x08                    /* code32 selector */

        .code32
.Lpm32:
        movw    $0x10, %ax
        movw    %ax, %ds
        movw    %ax, %es
        movw    %ax, %fs
        movw    %ax, %gs
        movw    %ax, %ss
        movl    $0x7c00, %esp
        jmp     *%ebx

        .code16
.Lret:
        popw    %es
        popw    %ds
        popal
        lretw

        .align  8
gdt:
        .quad   0                       /* null */
        .quad   0x00cf9a000000ffff      /* 0x08: code32 flat 4G */
        .quad   0x00cf92000000ffff      /* 0x10: data32 flat 4G */
gdt_end:

gdtr:
        .word   gdt_end - gdt - 1
        .long   0                       /* base — patched */

        .org    511
        .byte   0                       /* checksum — patched at build time */
end:
