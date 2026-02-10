; MiniOS User-space Entry Point
; This is the first code executed in a user program.
; It sets up the environment and calls main().

bits 32

section .text
global _start
extern main
extern exit

; _start - Entry point for user programs
; Called by kernel when process starts
_start:
    ; Clear EBP for stack traces
    xor ebp, ebp
    
    ; Align stack to 16 bytes (for SSE if ever used)
    and esp, 0xFFFFFFF0
    
    ; Call main(argc, argv)
    ; For now, we don't pass arguments
    ; TODO: Parse arguments from stack
    push 0          ; argv = NULL
    push 0          ; argc = 0
    call main
    add esp, 8      ; Clean up args
    
    ; Exit with return value from main
    push eax
    call exit
    
    ; Should never reach here
.hang:
    hlt
    jmp .hang

