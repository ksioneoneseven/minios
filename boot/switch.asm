; ============================================================================
; MiniOS Context Switch Assembly
; ============================================================================
; Implements low-level context switching between processes.
; ============================================================================

section .text

; void switch_to(process_t* prev, process_t* next)
; prev at [esp+4], next at [esp+8]
;
; Process structure offsets (must match process.h):
;   +0:   pid
;   +4:   ppid
;   +8:   name[32]
;   +40:  state
;   +44:  exit_code
;   +48:  is_user_mode (1 byte, padded to 4 = ends at 52)
;   +52:  uid
;   +56:  gid
;   +60:  euid
;   +64:  egid
;   +68:  context (pointer to saved CPU state)
;   +72:  kernel_stack (top of kernel stack)
;   +76:  kernel_stack_base
;   +80:  user_stack
;   +84:  user_stack_base
;   +88:  user_entry
;   +92:  page_directory
;   +96:  heap_start
;   +100: heap_break
;   +104: fd_table[16] (64 bytes)

global switch_to
switch_to:
    ; Save callee-saved registers on current stack
    push ebp
    push ebx
    push esi
    push edi

    ; Get prev and next pointers
    mov eax, [esp + 20]     ; prev (offset by 4 pushes = 16 bytes + return addr = 20)
    mov edx, [esp + 24]     ; next

    ; If prev is NULL, skip saving
    test eax, eax
    jz .load_next

    ; Save current stack pointer to prev->context
    mov [eax + 68], esp     ; prev->context = esp

.load_next:
    ; Load next process's stack pointer
    mov esp, [edx + 68]     ; esp = next->context

    ; Load next process's page directory
    mov eax, [edx + 92]     ; eax = next->page_directory
    mov cr3, eax            ; Switch page directory

    ; Restore callee-saved registers
    pop edi
    pop esi
    pop ebx
    pop ebp

    ret                     ; Return to next process's saved EIP

; ============================================================================
; First process entry helper
; ============================================================================
; When a process is first scheduled, we need to set up the stack
; so it looks like switch_to returned into the process entry point.
;
; This is called from the context setup in process_create.
; The stack is set up with the entry point as the return address.

global process_entry
process_entry:
    ; Enable interrupts (they might be disabled from switch_to)
    sti

    ; The entry point address is at the top of the stack (put there by process_create)
    ret                     ; "Return" to entry point

; ============================================================================
; Enter User Mode
; ============================================================================
; void enter_user_mode(uint32_t entry_point, uint32_t user_stack)
; Transitions from kernel mode (ring 0) to user mode (ring 3)
; Uses iret to switch privilege levels
;
; Stack frame for iret (from low to high address):
;   EIP    - entry point
;   CS     - user code segment (0x1B = 0x18 | 3)
;   EFLAGS - flags with interrupts enabled
;   ESP    - user stack pointer
;   SS     - user data segment (0x23 = 0x20 | 3)

global enter_user_mode
enter_user_mode:
    ; Get arguments
    mov eax, [esp + 4]      ; entry_point
    mov ebx, [esp + 8]      ; user_stack

    ; Set up segment registers for user mode
    mov cx, 0x23            ; User data segment (0x20 | RPL 3)
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    ; Build iret stack frame
    push dword 0x23         ; SS - user data segment
    push ebx                ; ESP - user stack
    pushfd                  ; EFLAGS
    pop ecx                 ; Get EFLAGS
    or ecx, 0x200           ; Set IF (interrupt enable flag)
    push ecx                ; Push modified EFLAGS
    push dword 0x1B         ; CS - user code segment (0x18 | RPL 3)
    push eax                ; EIP - entry point

    ; Return to user mode
    iret
