/*
 * MiniOS ELF Loader Implementation
 * 
 * Loads ELF32 executables into process address space.
 */

#include "../include/elf.h"
#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/vfs.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"

/*
 * Validate an ELF header
 */
bool elf_validate(const elf32_ehdr_t* ehdr) {
    /* Check magic number */
    if (ehdr->e_ident[EI_MAG0] != ELF_MAG0 ||
        ehdr->e_ident[EI_MAG1] != ELF_MAG1 ||
        ehdr->e_ident[EI_MAG2] != ELF_MAG2 ||
        ehdr->e_ident[EI_MAG3] != ELF_MAG3) {
        printk("ELF: Invalid magic number\n");
        return false;
    }
    
    /* Check class (must be 32-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        printk("ELF: Not a 32-bit executable\n");
        return false;
    }
    
    /* Check endianness (must be little-endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        printk("ELF: Not little-endian\n");
        return false;
    }
    
    /* Check file type (must be executable) */
    if (ehdr->e_type != ET_EXEC) {
        printk("ELF: Not an executable file (type=%d)\n", ehdr->e_type);
        return false;
    }
    
    /* Check machine type (must be i386) */
    if (ehdr->e_machine != EM_386) {
        printk("ELF: Not an i386 executable (machine=%d)\n", ehdr->e_machine);
        return false;
    }
    
    /* Check for program headers */
    if (ehdr->e_phnum == 0) {
        printk("ELF: No program headers\n");
        return false;
    }
    
    return true;
}

/*
 * Load an ELF executable into current address space
 */
int elf_load(const void* data, size_t size, uint32_t* entry) {
    const elf32_ehdr_t* ehdr = (const elf32_ehdr_t*)data;
    
    /* Validate the ELF header */
    if (!elf_validate(ehdr)) {
        return -1;
    }
    
    /* Check that program header table is within file */
    if (ehdr->e_phoff + (ehdr->e_phnum * sizeof(elf32_phdr_t)) > size) {
        printk("ELF: Program header table beyond file end\n");
        return -1;
    }
    
    /* Get program headers */
    const elf32_phdr_t* phdr = (const elf32_phdr_t*)((uint8_t*)data + ehdr->e_phoff);
    
    /* Load each PT_LOAD segment */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;  /* Skip non-loadable segments */
        }
        
        uint32_t vaddr = phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;
        uint32_t flags = phdr[i].p_flags;
        
        /* Validate segment is within file */
        if (offset + filesz > size) {
            printk("ELF: Segment %d beyond file end\n", i);
            return -1;
        }
        
        /* Validate segment is in user space */
        if (vaddr < 0x1000 || vaddr >= 0xC0000000) {
            printk("ELF: Segment %d has invalid address 0x%08X\n", i, vaddr);
            return -1;
        }
        
        /* Determine page flags */
        uint32_t page_flags = PAGE_USER;
        if (flags & PF_W) {
            page_flags |= PAGE_WRITE;
        }
        
        /* Map pages for this segment */
        uint32_t start_page = PAGE_ALIGN_DOWN(vaddr);
        uint32_t end_page = PAGE_ALIGN_UP(vaddr + memsz);
        
        for (uint32_t page = start_page; page < end_page; page += PAGE_SIZE) {
            /* Allocate physical frame */
            uint32_t frame = pmm_alloc_frame();
            if (frame == 0) {
                printk("ELF: Out of memory\n");
                return -1;
            }
            
            /* Map the page */
            paging_map_page(page, frame, page_flags);
            
            /* Zero the page first */
            memset((void*)page, 0, PAGE_SIZE);
        }
        
        /* Copy segment data */
        if (filesz > 0) {
            const uint8_t* src = (const uint8_t*)data + offset;
            memcpy((void*)vaddr, src, filesz);
        }
        
        /* Note: BSS section (memsz > filesz) is already zeroed above */
        
        printk("ELF: Loaded segment %d: vaddr=0x%08X memsz=0x%X\n", 
               i, vaddr, memsz);
    }
    
    /* Return entry point */
    *entry = ehdr->e_entry;
    printk("ELF: Entry point at 0x%08X\n", *entry);

    return 0;
}

/*
 * Load an ELF executable from VFS
 */
int elf_load_file(const char* path, uint32_t* entry) {
    /* Look up the file */
    vfs_node_t* node = vfs_lookup(path);
    if (node == NULL) {
        printk("ELF: File not found: %s\n", path);
        return -1;
    }

    /* Get file size */
    size_t size = node->length;
    if (size == 0) {
        printk("ELF: File is empty: %s\n", path);
        return -1;
    }

    /* Allocate buffer for file contents */
    void* data = kmalloc(size);
    if (data == NULL) {
        printk("ELF: Cannot allocate %u bytes for file\n", (uint32_t)size);
        return -1;
    }

    /* Read file contents */
    ssize_t read = vfs_read(node, 0, size, (uint8_t*)data);
    if (read < 0 || (size_t)read != size) {
        printk("ELF: Failed to read file\n");
        kfree(data);
        return -1;
    }

    /* Load the ELF */
    int result = elf_load(data, size, entry);

    /* Free the buffer */
    kfree(data);

    return result;
}

