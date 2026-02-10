; ============================================================================
; MiniOS Interrupt Assembly Stubs
; ============================================================================
; Provides low-level interrupt entry points that save CPU state,
; call the C handler, and restore state before returning.
; ============================================================================

section .text

; External C handler
extern isr_handler
extern irq_handler

; ============================================================================
; IDT flush - Load the IDT
; ============================================================================
global idt_flush
idt_flush:
    mov eax, [esp + 4]      ; Get pointer to IDT descriptor
    lidt [eax]              ; Load IDT
    ret

; ============================================================================
; Common ISR stub - saves state and calls C handler
; ============================================================================
isr_common_stub:
    pusha                   ; Push all general-purpose registers

    mov ax, ds              ; Save data segment
    push eax

    mov ax, 0x10            ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                ; Push pointer to registers struct
    call isr_handler        ; Call C handler
    add esp, 4              ; Clean up pushed pointer

    pop eax                 ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Ensure interrupts will be enabled when we return to user mode
    ; EFLAGS is at esp+48: 8 pusha regs (32) + int_no (4) + err_code (4) + eip (4) + cs (4) = 48
    or dword [esp + 48], 0x200  ; Set IF in EFLAGS

    popa                    ; Restore general-purpose registers
    add esp, 8              ; Clean up error code and ISR number
    iret                    ; Return from interrupt

; ============================================================================
; Common IRQ stub - saves state and calls C handler
; ============================================================================
irq_common_stub:
    pusha                   ; Push all general-purpose registers

    mov ax, ds              ; Save data segment
    push eax

    mov ax, 0x10            ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                ; Push pointer to registers struct
    call irq_handler        ; Call C handler
    add esp, 4              ; Clean up pushed pointer

    pop eax                 ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Ensure interrupts will be enabled when we return
    ; Set IF bit in saved EFLAGS (which is at [esp + 44] after popa cleanup)
    ; After popa: esp points to int_no, err_code, eip, cs, eflags
    ; So EFLAGS is at esp + 8 (after we do "add esp, 8" to skip int_no and err_code)
    ; But we need to do it before add esp, 8, so it's at esp + 8 + 8 = esp + 16
    ; Actually: after popa, stack is: int_no, err_code, eip, cs, eflags, useresp, ss
    ; So EFLAGS is at [esp + 16] (after popa, before add esp, 8)
    or dword [esp + 48], 0x200  ; Set IF in EFLAGS (48 = 8*4 pusha regs + ds + int_no + err_code + eip + cs)
    
    popa                    ; Restore general-purpose registers
    add esp, 8              ; Clean up error code and IRQ number
    iret                    ; Return from interrupt

; ============================================================================
; ISR stubs (0-31) - CPU Exceptions
; ============================================================================
; Some exceptions push an error code, others don't.
; We push a dummy error code (0) for those that don't.
; ============================================================================

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0            ; Dummy error code
    push dword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1           ; Interrupt number (error code already pushed)
    jmp isr_common_stub
%endmacro

; CPU Exceptions
ISR_NOERRCODE 0             ; Divide by zero
ISR_NOERRCODE 1             ; Debug
ISR_NOERRCODE 2             ; NMI
ISR_NOERRCODE 3             ; Breakpoint
ISR_NOERRCODE 4             ; Overflow
ISR_NOERRCODE 5             ; Bound range exceeded
ISR_NOERRCODE 6             ; Invalid opcode
ISR_NOERRCODE 7             ; Device not available
ISR_ERRCODE   8             ; Double fault
ISR_NOERRCODE 9             ; Coprocessor segment overrun
ISR_ERRCODE   10            ; Invalid TSS
ISR_ERRCODE   11            ; Segment not present
ISR_ERRCODE   12            ; Stack-segment fault
ISR_ERRCODE   13            ; General protection fault
ISR_ERRCODE   14            ; Page fault
ISR_NOERRCODE 15            ; Reserved
ISR_NOERRCODE 16            ; x87 FPU error
ISR_ERRCODE   17            ; Alignment check
ISR_NOERRCODE 18            ; Machine check
ISR_NOERRCODE 19            ; SIMD exception
ISR_NOERRCODE 20            ; Virtualization exception
ISR_NOERRCODE 21            ; Reserved
ISR_NOERRCODE 22            ; Reserved
ISR_NOERRCODE 23            ; Reserved
ISR_NOERRCODE 24            ; Reserved
ISR_NOERRCODE 25            ; Reserved
ISR_NOERRCODE 26            ; Reserved
ISR_NOERRCODE 27            ; Reserved
ISR_NOERRCODE 28            ; Reserved
ISR_NOERRCODE 29            ; Reserved
ISR_NOERRCODE 30            ; Reserved
ISR_NOERRCODE 31            ; Reserved

; ============================================================================
; IRQ stubs (0-15) - Hardware Interrupts
; ============================================================================
%macro IRQ 2
global irq%1
irq%1:
    push dword 0            ; Dummy error code
    push dword %2           ; Interrupt number (32 + IRQ number)
    jmp irq_common_stub
%endmacro

IRQ 0, 32                   ; Timer
IRQ 1, 33                   ; Keyboard
IRQ 2, 34                   ; Cascade
IRQ 3, 35                   ; COM2
IRQ 4, 36                   ; COM1
IRQ 5, 37                   ; LPT2
IRQ 6, 38                   ; Floppy
IRQ 7, 39                   ; LPT1 / Spurious
IRQ 8, 40                   ; RTC
IRQ 9, 41                   ; Free
IRQ 10, 42                  ; Free
IRQ 11, 43                  ; Free
IRQ 12, 44                  ; PS/2 Mouse
IRQ 13, 45                  ; FPU
IRQ 14, 46                  ; Primary ATA
IRQ 15, 47                  ; Secondary ATA

; ============================================================================
; System Call Interrupt (INT 0x80)
; ============================================================================
global isr128
isr128:
    push dword 0            ; Dummy error code
    push dword 128          ; Interrupt number (0x80)
    jmp isr_common_stub
