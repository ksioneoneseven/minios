/*
 * MiniOS ELF (Executable and Linkable Format) Header
 * 
 * Definitions for parsing ELF32 binaries.
 */

#ifndef _ELF_H
#define _ELF_H

#include "types.h"

/* ELF Magic bytes */
#define ELF_MAGIC       0x464C457F  /* "\x7FELF" in little-endian */
#define ELF_MAG0        0x7F
#define ELF_MAG1        'E'
#define ELF_MAG2        'L'
#define ELF_MAG3        'F'

/* ELF class (32-bit vs 64-bit) */
#define ELFCLASS32      1
#define ELFCLASS64      2

/* ELF data encoding (endianness) */
#define ELFDATA2LSB     1           /* Little-endian */
#define ELFDATA2MSB     2           /* Big-endian */

/* ELF file types */
#define ET_NONE         0           /* Unknown */
#define ET_REL          1           /* Relocatable */
#define ET_EXEC         2           /* Executable */
#define ET_DYN          3           /* Shared object */
#define ET_CORE         4           /* Core dump */

/* ELF machine types */
#define EM_386          3           /* Intel 80386 */
#define EM_X86_64       62          /* AMD x86-64 */

/* Program header types */
#define PT_NULL         0           /* Unused entry */
#define PT_LOAD         1           /* Loadable segment */
#define PT_DYNAMIC      2           /* Dynamic linking info */
#define PT_INTERP       3           /* Interpreter path */
#define PT_NOTE         4           /* Auxiliary info */
#define PT_SHLIB        5           /* Reserved */
#define PT_PHDR         6           /* Program header table */

/* Program header flags */
#define PF_X            0x1         /* Executable */
#define PF_W            0x2         /* Writable */
#define PF_R            0x4         /* Readable */

/* ELF32 header structure */
typedef struct {
    uint8_t  e_ident[16];       /* ELF identification */
    uint16_t e_type;            /* Object file type */
    uint16_t e_machine;         /* Machine type */
    uint32_t e_version;         /* Object file version */
    uint32_t e_entry;           /* Entry point address */
    uint32_t e_phoff;           /* Program header offset */
    uint32_t e_shoff;           /* Section header offset */
    uint32_t e_flags;           /* Processor-specific flags */
    uint16_t e_ehsize;          /* ELF header size */
    uint16_t e_phentsize;       /* Size of program header entry */
    uint16_t e_phnum;           /* Number of program header entries */
    uint16_t e_shentsize;       /* Size of section header entry */
    uint16_t e_shnum;           /* Number of section header entries */
    uint16_t e_shstrndx;        /* Section name string table index */
} __attribute__((packed)) elf32_ehdr_t;

/* ELF32 program header structure */
typedef struct {
    uint32_t p_type;            /* Type of segment */
    uint32_t p_offset;          /* Offset in file */
    uint32_t p_vaddr;           /* Virtual address in memory */
    uint32_t p_paddr;           /* Physical address (unused) */
    uint32_t p_filesz;          /* Size of segment in file */
    uint32_t p_memsz;           /* Size of segment in memory */
    uint32_t p_flags;           /* Segment flags */
    uint32_t p_align;           /* Alignment */
} __attribute__((packed)) elf32_phdr_t;

/* ELF32 section header structure */
typedef struct {
    uint32_t sh_name;           /* Section name (string table index) */
    uint32_t sh_type;           /* Section type */
    uint32_t sh_flags;          /* Section flags */
    uint32_t sh_addr;           /* Section virtual address */
    uint32_t sh_offset;         /* Section file offset */
    uint32_t sh_size;           /* Section size */
    uint32_t sh_link;           /* Link to another section */
    uint32_t sh_info;           /* Additional info */
    uint32_t sh_addralign;      /* Alignment */
    uint32_t sh_entsize;        /* Entry size if fixed-size entries */
} __attribute__((packed)) elf32_shdr_t;

/* ELF ident indices */
#define EI_MAG0         0       /* Magic number byte 0 */
#define EI_MAG1         1       /* Magic number byte 1 */
#define EI_MAG2         2       /* Magic number byte 2 */
#define EI_MAG3         3       /* Magic number byte 3 */
#define EI_CLASS        4       /* File class */
#define EI_DATA         5       /* Data encoding */
#define EI_VERSION      6       /* File version */
#define EI_OSABI        7       /* OS/ABI identification */
#define EI_ABIVERSION   8       /* ABI version */
#define EI_PAD          9       /* Start of padding bytes */

/*
 * Validate an ELF header
 * Returns true if valid ELF32 executable for i386
 */
bool elf_validate(const elf32_ehdr_t* ehdr);

/*
 * Load an ELF executable into a process's address space
 * data: pointer to ELF file data
 * size: size of ELF file
 * entry: output pointer to store entry point
 * Returns: 0 on success, -1 on error
 */
int elf_load(const void* data, size_t size, uint32_t* entry);

/*
 * Load an ELF executable from VFS
 * path: path to executable
 * entry: output pointer to store entry point
 * Returns: 0 on success, -1 on error
 */
int elf_load_file(const char* path, uint32_t* entry);

#endif /* _ELF_H */

