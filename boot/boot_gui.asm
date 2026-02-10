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

; Video mode request constants
VIDEO_MODE_TYPE     equ 0               ; 0 = linear graphics mode
VIDEO_WIDTH         equ 800             ; Requested width
VIDEO_HEIGHT        equ 600             ; Requested height
VIDEO_DEPTH         equ 32              ; Bits per pixel (32bpp ARGB)

; ============================================================================
; Multiboot Header Section
; Must be within first 8KB of kernel image
; ============================================================================
section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
    ; Address fields (not used with GRUB, set to 0)
    ; Video mode request fields
    dd VIDEO_MODE_TYPE          ; mode_type (0 = linear graphics)
    dd VIDEO_WIDTH              ; width
    dd VIDEO_HEIGHT             ; height
    dd VIDEO_DEPTH              ; depth (bpp)

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
