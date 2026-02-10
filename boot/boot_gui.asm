; ============================================================================
; MiniOS Bootloader Entry Point (GUI Mode)
; ============================================================================
; This version requests VESA graphics mode via multiboot.
; ============================================================================

; Multiboot header constants
MULTIBOOT_MAGIC     equ 0x1BADB002      ; Magic number for bootloader
MULTIBOOT_ALIGN     equ 1 << 0          ; Align modules on page boundaries
MULTIBOOT_MEMINFO   equ 1 << 1          ; Request memory map from GRUB
MULTIBOOT_VIDEO     equ 1 << 2          ; Request video mode info
MULTIBOOT_FLAGS     equ MULTIBOOT_ALIGN | MULTIBOOT_MEMINFO | MULTIBOOT_VIDEO
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; ============================================================================
; Multiboot Header Section
; Must be within first 8KB of kernel image
; ============================================================================
section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
    ; Address fields (unused when loaded as ELF, but must be present for
    ; the video fields to be at the correct offset in the header)
    dd 0                        ; header_addr
    dd 0                        ; load_addr
    dd 0                        ; load_end_addr
    dd 0                        ; bss_end_addr
    dd 0                        ; entry_addr
    ; Video mode request fields — all zeros = accept whatever GRUB sets
    dd 0                        ; mode_type (0 = linear graphics)
    dd 0                        ; width  (0 = no preference)
    dd 0                        ; height (0 = no preference)
    dd 0                        ; depth  (0 = no preference)

; ============================================================================
; BSS Section - Uninitialized Data
; ============================================================================
section .bss
align 16
stack_bottom:
    resb 65536              ; 64 KB stack
stack_top:

; ============================================================================
; Text Section - Code
; ============================================================================
section .text
global _start
extern kernel_main

; Entry point - called by GRUB
_start:
    ; Set up the stack pointer
    mov esp, stack_top

    ; Push multiboot info pointer (in EBX from GRUB)
    push ebx
    
    ; Push multiboot magic number (in EAX from GRUB)
    push eax
    
    ; Call the C kernel main function
    ; void kernel_main(uint32_t magic, multiboot_info_t* mboot_info)
    call kernel_main
    
    ; If kernel_main returns, halt the CPU
    cli                     ; Disable interrupts

.hang:
    hlt                     ; Halt the CPU
    jmp .hang               ; Loop forever (in case of NMI)
