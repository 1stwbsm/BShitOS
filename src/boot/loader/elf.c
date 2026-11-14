#include "lib/lib.h"
#include "loader/elf.h"

#define PAGE_SIZE 4096u
#define PT_LOAD 1
#define ET_EXEC 2
#define EM_386 3

typedef struct {
    UINT8 e_ident[16];
    UINT16 e_type, e_machine;
    UINT32 e_version, e_entry, e_phoff, e_shoff, e_flags;
    UINT16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    UINT32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;

EFI_STATUS elf_load(const void *image, UINTN size, UINT32 *entry)
{
    const UINT8 *bytes = image;
    const Elf32_Ehdr *eh = image;
    const Elf32_Phdr *ph;
    UINT64 low = ~0ULL, high = 0;
    EFI_PHYSICAL_ADDRESS base;
    EFI_STATUS status;
    BOOLEAN have_entry = 0;

    if (size < sizeof(*eh) || memcmp(eh->e_ident, "\177ELF\1\1", 6) != 0 ||
        eh->e_type != ET_EXEC || eh->e_machine != EM_386 ||
        eh->e_phentsize != sizeof(Elf32_Phdr) ||
        (UINT64)eh->e_phoff + (UINT64)eh->e_phnum * sizeof(Elf32_Phdr) > size)
        return EFI_LOAD_ERROR;

    ph = (const Elf32_Phdr *)(bytes + eh->e_phoff);

    /* Validate everything first so a bad file leaves memory untouched. */
    for (UINT16 i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            (UINT64)ph[i].p_offset + ph[i].p_filesz > size ||
            (UINT64)ph[i].p_paddr + ph[i].p_memsz > 0x100000000ULL)
            return EFI_LOAD_ERROR;
        if (ph[i].p_paddr < low)
            low = ph[i].p_paddr;
        if ((UINT64)ph[i].p_paddr + ph[i].p_memsz > high)
            high = (UINT64)ph[i].p_paddr + ph[i].p_memsz;
        if (eh->e_entry >= ph[i].p_vaddr && eh->e_entry - ph[i].p_vaddr < ph[i].p_memsz) {
            *entry = eh->e_entry - ph[i].p_vaddr + ph[i].p_paddr;
            have_entry = 1;
        }
    }
    if (!have_entry)
        return EFI_LOAD_ERROR;

    /* One allocation spanning all segments: they may share pages. */
    base = low & ~(UINT64)(PAGE_SIZE - 1);
    UINTN pages = (UINTN)((high - base + PAGE_SIZE - 1) / PAGE_SIZE);
    status = gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &base);
    if (EFI_ERROR(status))
        return status;

    memset((void *)(UINTN)base, 0, pages * PAGE_SIZE);
    for (UINT16 i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD && ph[i].p_memsz != 0)
            memcpy((void *)(UINTN)ph[i].p_paddr, bytes + ph[i].p_offset, ph[i].p_filesz);
    return EFI_SUCCESS;
}
