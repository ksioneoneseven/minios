; ============================================================================
; MiniOS GDT Assembly Functions
; ============================================================================
; Provides the gdt_flush and tss_flush functions called from C code.
; These load the GDT and TSS using the LGDT and LTR instructions.
; ============================================================================

section .text

; ============================================================================
; gdt_flush - Load the GDT and reload segment registers
; ============================================================================
; void gdt_flush(uint32_t gdt_ptr)
; Argument: pointer to GDT descriptor structure
; ============================================================================
global gdt_flush
gdt_flush:
    mov eax, [esp + 4]      ; Get pointer to GDT descriptor
    lgdt [eax]              ; Load GDT
    
    ; Reload segment registers with new selectors
    mov ax, 0x10            ; Kernel data segment (offset 0x10 in GDT)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to reload CS with kernel code segment
    jmp 0x08:.flush         ; 0x08 = kernel code segment
.flush:
    ret

; ============================================================================
; tss_flush - Load the Task State Segment
; ============================================================================
; void tss_flush(void)
; Loads the TSS selector (0x28) into the task register
; ============================================================================
global tss_flush
tss_flush:
    mov ax, 0x2B            ; TSS selector (0x28) with RPL 3
    ltr ax                  ; Load task register
    ret

