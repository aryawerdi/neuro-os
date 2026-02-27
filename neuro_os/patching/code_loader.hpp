#ifndef NEURO_OS_PATCHING_CODE_LOADER_HPP
#define NEURO_OS_PATCHING_CODE_LOADER_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <functional>

#if defined(__APPLE__)
    #include <mach-o/loader.h>
    #include <mach-o/nlist.h>
    #include <mach-o/stab.h>
#elif defined(__linux__)
    #include <elf.h>
    #include <link.h>
#endif

namespace neuro_os::patching {

struct RelocationInfo {
    uintptr_t address;
    uintptr_t value;
    size_t size;
    bool is_relative;
};

struct SymbolInfo {
    std::string name;
    uintptr_t address;
    size_t size;
};

class CodeLoader {
private:
    std::vector<uint8_t> loaded_code_;
    uintptr_t base_address_ = 0;
    std::unordered_map<std::string, SymbolInfo> symbols_;
    std::unordered_map<uintptr_t, RelocationInfo> relocations_;

#if defined(__APPLE__)
    bool parse_mach_o(const std::vector<uint8_t>& data);
#elif defined(__linux__)
    bool parse_elf(const std::vector<uint8_t>& data);
#endif

public:
    CodeLoader();
    ~CodeLoader() = default;

    std::vector<uint8_t> load_from_file(const std::string& path);
    std::vector<uint8_t> load_from_memory(const uint8_t* data, size_t size);
    std::vector<uint8_t> load_from_memory(const std::vector<uint8_t>& data);

    void relocate(std::vector<uint8_t>& code, uintptr_t base_address);
    void apply_relocations();

    void* resolve_symbol(const std::string& name);
    const SymbolInfo* get_symbol(const std::string& name) const;
    std::vector<std::string> get_all_symbols() const;

    uintptr_t get_base_address() const;
    const std::vector<uint8_t>& get_code() const;

    void add_symbol(const std::string& name, uintptr_t address, size_t size = 0);
    void add_relocation(uintptr_t address, uintptr_t value, size_t size, bool is_relative = false);

    bool is_loaded() const;
    void clear();
};

inline CodeLoader::CodeLoader() = default;

#if defined(__APPLE__)
inline bool CodeLoader::parse_mach_o(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(mach_header_64)) {
        return false;
    }

    const mach_header_64* header = reinterpret_cast<const mach_header_64*>(data.data());

    if (header->magic != MH_MAGIC_64) {
        return false;
    }

    uint64_t offset = sizeof(mach_header_64);

    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (offset + sizeof(load_command) > data.size()) {
            break;
        }

        const load_command* cmd = reinterpret_cast<const load_command*>(data.data() + offset);

        if (cmd->cmd == LC_SYMTAB) {
            const symtab_command* symtab = reinterpret_cast<const symtab_command*>(cmd);
            
            if (symtab->symoff > 0 && symtab->nsyms > 0 && symtab->symoff + symtab->nsyms * sizeof(nlist_64) <= data.size()) {
                const nlist_64* symbols = reinterpret_cast<const nlist_64*>(data.data() + symtab->symoff);
                const char* strtable = reinterpret_cast<const char*>(data.data() + symtab->stroff);

                for (uint32_t j = 0; j < symtab->nsyms; ++j) {
                    if (symbols[j].n_type & N_STAB) {
                        continue;
                    }

                    if (symbols[j].n_un.n_strx > 0) {
                        std::string name = strtable + symbols[j].n_un.n_strx;
                        if (!name.empty()) {
                            if (name[0] == '_') {
                                name = name.substr(1);
                            }
                            symbols_[name] = {name, symbols[j].n_value, 0};
                        }
                    }
                }
            }
        }

        offset += cmd->cmdsize;
    }

    return true;
}
#elif defined(__linux__)
inline bool CodeLoader::parse_elf(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(Elf64_Ehdr)) {
        return false;
    }

    const Elf64_Ehdr* header = reinterpret_cast<const Elf64_Ehdr*>(data.data());

    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    Elf64_Shdr* shdr = reinterpret_cast<Elf64_Shdr*>(data.data() + header->e_shoff);
    const char* shstrtab = reinterpret_cast<const char*>(data.data() + shdr[header->e_shstrndx].sh_offset);

    for (int i = 0; i < header->e_shnum; ++i) {
        std::string section_name = shstrtab + shdr[i].sh_name;

        if (section_name == ".symtab") {
            Elf64_Sym* symbols = reinterpret_cast<Elf64_Sym*>(data.data() + shdr[i].sh_offset);
            size_t symbol_count = shdr[i].sh_size / sizeof(Elf64_Sym);

            for (size_t j = 0; j < symbol_count; ++j) {
                if (symbols[j].st_name > 0) {
                    const char* name = reinterpret_cast<const char*>(data.data() + shdr[header->e_shstrndx].sh_offset + symbols[j].st_name);
                    if (name && *name) {
                        symbols_[name] = {name, symbols[j].st_value, symbols[j].st_size};
                    }
                }
            }
        }
    }

    return true;
}
#endif

inline std::vector<uint8_t> CodeLoader::load_from_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0) {
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }

    loaded_code_ = buffer;
    
#if defined(__APPLE__)
    parse_mach_o(buffer);
#elif defined(__linux__)
    parse_elf(buffer);
#endif

    return buffer;
}

inline std::vector<uint8_t> CodeLoader::load_from_memory(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }

    loaded_code_.resize(size);
    std::copy(data, data + size, loaded_code_.begin());

    return loaded_code_;
}

inline std::vector<uint8_t> CodeLoader::load_from_memory(const std::vector<uint8_t>& data) {
    return load_from_memory(data.data(), data.size());
}

inline void CodeLoader::relocate(std::vector<uint8_t>& code, uintptr_t base_address) {
    if (code.empty()) {
        return;
    }

    base_address_ = base_address;
    
    for (auto& [_, reloc] : relocations_) {
        if (reloc.is_relative) {
            uintptr_t target = base_address + reloc.value;
            
            if (reloc.size == 8) {
                std::copy(reinterpret_cast<const uint8_t*>(&target),
                         reinterpret_cast<const uint8_t*>(&target) + 8,
                         code.begin() + reloc.address);
            } else if (reloc.size == 4) {
                uint32_t target32 = static_cast<uint32_t>(target);
                std::copy(reinterpret_cast<const uint8_t*>(&target32),
                         reinterpret_cast<const uint8_t*>(&target32) + 4,
                         code.begin() + reloc.address);
            }
        }
    }
}

inline void CodeLoader::apply_relocations() {
    if (loaded_code_.empty()) {
        return;
    }

    relocate(loaded_code_, base_address_);
}

inline void* CodeLoader::resolve_symbol(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return reinterpret_cast<void*>(it->second.address);
    }
    return nullptr;
}

inline const SymbolInfo* CodeLoader::get_symbol(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return &(it->second);
    }
    return nullptr;
}

inline std::vector<std::string> CodeLoader::get_all_symbols() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : symbols_) {
        result.push_back(name);
    }
    return result;
}

inline uintptr_t CodeLoader::get_base_address() const {
    return base_address_;
}

inline const std::vector<uint8_t>& CodeLoader::get_code() const {
    return loaded_code_;
}

inline void CodeLoader::add_symbol(const std::string& name, uintptr_t address, size_t size) {
    symbols_[name] = {name, address, size};
}

inline void CodeLoader::add_relocation(uintptr_t address, uintptr_t value, size_t size, bool is_relative) {
    relocations_[address] = {address, value, size, is_relative};
}

inline bool CodeLoader::is_loaded() const {
    return !loaded_code_.empty();
}

inline void CodeLoader::clear() {
    loaded_code_.clear();
    symbols_.clear();
    relocations_.clear();
    base_address_ = 0;
}

}

#endif
