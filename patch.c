// Source: https://gist.github.com/jhftss/729aea25511439dc34f0fdfa158be9b6
// Website: https://jhftss.github.io/Run-any-iOS-Apps-in-the-Xcode-Simulator/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach/machine.h>

static unsigned long max_header_size = 65535;

uint32_t swap_uint32(uint32_t val) {
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
}

uint64_t swap_uint64(uint64_t val) {
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL) | ((val >> 8) & 0x00FF00FF00FF00FFULL);
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL) | ((val >> 16) & 0x0000FFFF0000FFFFULL);
    return (val << 32) | (val >> 32);
}

int patch_for_simulator(char *base, char *err_msg) {
    static struct {
        struct build_version_command cmd;
        struct build_tool_version tool_ver;
    } buildVersionForSimulator = {LC_BUILD_VERSION, 0x20, 7, 0xE0000, 0xE0200, 1, 3, 0x2610700};
    struct load_command *lc;

    struct mach_header_64 *header = (struct mach_header_64 *) base;
    if (header->magic == FAT_CIGAM_64 || header->magic == FAT_CIGAM) {
        struct fat_header *fat = (struct fat_header *) base;
        if (swap_uint32(fat->nfat_arch) != 1) {
            snprintf(err_msg, 1024, "error: iOS App has fat macho with more than one architecture? (%d)\n",
                     swap_uint32(fat->nfat_arch));
            return 0;
        }
        cpu_type_t cputype;
        uint64_t offset;
        if (header->magic == FAT_CIGAM_64) {
            struct fat_arch_64 *farch64 = (struct fat_arch_64 *) (base + sizeof(struct fat_header));
            cputype = swap_uint32(farch64->cputype);
            offset = swap_uint64(farch64->offset);
        } else {
            struct fat_arch *farch = (struct fat_arch *) (base + sizeof(struct fat_header));
            cputype = swap_uint32(farch->cputype);
            offset = swap_uint32(farch->offset);
        }

        if (cputype != CPU_TYPE_ARM64) {
            snprintf(err_msg, 1024, "error: iOS App has macho with wrong cputype:0x%x\n", cputype);
            return 0;
        }
        if (offset > max_header_size) {
            snprintf(err_msg, 1024,
                     "error: huge fat arch offset 0x%llx > 0x%lx, set MAX_HEADER_SIZE environment to change the default\n",
                     offset, max_header_size);
        }
        header = (struct mach_header_64 *) (base + offset);
    }
    if (header->magic != MH_MAGIC_64) {
        snprintf(err_msg, 1024, "error: not a valid macho file\n");
        return 0;
    }

    // load commands begin after the macho header
    lc = (struct load_command *) ((mach_vm_address_t) header + sizeof(struct mach_header_64));
    uint32_t removedSize = 0, sizeofcmds = 0, numOfRemoved = 0, i = 0, cmdsize = 0;
    int found_build_version_command = 0, removed = 0;
    for (; i < header->ncmds; i++) {
        removed = 0;
        if (lc->cmd == LC_ENCRYPTION_INFO || lc->cmd == LC_ENCRYPTION_INFO_64 || lc->cmd == LC_VERSION_MIN_IPHONEOS) {
            // mark the load command as removed
            removed = 1;
            removedSize += lc->cmdsize;
            numOfRemoved += 1;
            // printf("remove load command[0x%x] at offset:0x%llx\n", lc->cmd, (mach_vm_address_t) lc - (mach_vm_address_t) header);
        } else if (lc->cmd == LC_BUILD_VERSION) {
            // replace build version with simulator version
            memcpy(lc, &buildVersionForSimulator,
                   sizeof(struct build_version_command) + sizeof(struct build_tool_version));
            found_build_version_command = 1;
            // printf("patch build version command at offset:0x%llx\n", (mach_vm_address_t) lc - (mach_vm_address_t) header);
        }
        // maybe overwrite, backup cmdsize
        cmdsize = lc->cmdsize;
        // move forward with removedSize bytes.
        if (removedSize && !removed) {
            memcpy((char *) lc - removedSize, lc, cmdsize);
        }
        sizeofcmds += cmdsize;
        lc = (struct load_command *) ((mach_vm_address_t) lc + cmdsize);
    }
    if (sizeofcmds != header->sizeofcmds) {
        snprintf(err_msg, 1024, "error: sizeofcmds(0x%x) != header->sizeofcmds(0x%x)\n", sizeofcmds,
                 header->sizeofcmds);
        return 0;
    }

    // not found, then insert one
    if (!found_build_version_command) {
        memcpy((char *) lc - removedSize, &buildVersionForSimulator,
               sizeof(struct build_version_command) + sizeof(struct build_tool_version));
        removedSize -= (sizeof(struct build_version_command) + sizeof(struct build_tool_version));
        numOfRemoved -= 1;
    }
    header->ncmds -= numOfRemoved;
    header->sizeofcmds -= removedSize;
    return 1;
}
