.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global g_pfnVectors
.global Reset_Handler
.extern SystemInit
.extern main
.extern _estack
.extern _sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler

    /* STM32F407 external IRQ slots 0..81.  This polling-only firmware does
       not enable them, so every slot safely enters Default_Handler. */
    .rept 82
    .word Default_Handler
    .endr
.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler,"ax",%progbits
.type Reset_Handler, %function
.thumb_func
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata

CopyData:
    cmp r1, r2
    bcs ClearBssStart
    ldr r3, [r0], #4
    str r3, [r1], #4
    b CopyData

ClearBssStart:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0

ClearBss:
    cmp r0, r1
    bcs StartApplication
    str r2, [r0], #4
    b ClearBss

StartApplication:
    bl SystemInit
    bl main

Hang:
    b Hang
.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.type Default_Handler, %function
.thumb_func
Default_Handler:
    b Default_Handler
.size Default_Handler, .-Default_Handler

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler
.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler
.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler

.section .note.GNU-stack,"",%progbits
