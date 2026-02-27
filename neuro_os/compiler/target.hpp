#ifndef NEURO_OS_COMPILER_TARGET_HPP
#define NEURO_OS_COMPILER_TARGET_HPP

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

#if __cplusplus >= 201703L
#include <optional>
#endif

namespace neuro_os::compiler {

enum class Architecture {
    X86_64,
    AArch64,
    ARM,
    RISCV64,
    WASM,
    Unknown
};

enum class OS {
    macOS,
    Linux,
    Windows,
    Unknown
};

enum class ABI {
    SysV,
    Win64,
    NEON,
    Unknown
};

struct CPUMicroarchitecture {
    std::string name;
    uint32_t family = 0;
    uint32_t model = 0;
    uint32_t stepping = 0;
};

struct SIMDInfo {
    bool sse = false;
    bool sse2 = false;
    bool sse3 = false;
    bool ssse3 = false;
    bool sse4_1 = false;
    bool sse4_2 = false;
    bool avx = false;
    bool avx2 = false;
    bool avx512f = false;
    bool avx512bw = false;
    bool avx512dq = false;
    bool neon = false;
    bool sve = false;
    
    bool has_vector() const {
        return avx || avx2 || avx512f || neon || sve;
    }
    
    size_t get_vector_width() const {
        if (avx512f) return 512;
        if (avx2 || avx) return 256;
        if (neon) return 128;
        if (sve) return 128;
        return 0;
    }
};

struct CacheInfo {
    uint32_t l1d_size = 0;
    uint32_t l1i_size = 0;
    uint32_t l2_size = 0;
    uint32_t l3_size = 0;
    uint32_t line_size = 0;
    
    uint32_t l1d_assoc = 0;
    uint32_t l2_assoc = 0;
};

class TargetMachine {
public:
    TargetMachine();
    explicit TargetMachine(const std::string& triple);
    
    static TargetMachine detect();
    
    Architecture getArchitecture() const { return arch_; }
    OS getOS() const { return os_; }
    ABI getABI() const { return abi_; }
    
    const std::string& getTriple() const { return triple_; }
    const std::string& getCPUName() const { return cpu_name_; }
    const CPUMicroarchitecture& getMicroarchitecture() const { return microarch_; }
    const SIMDInfo& getSIMDInfo() const { return simd_; }
    const CacheInfo& getCacheInfo() const { return cache_; }
    
    bool has_feature(const std::string& feature) const {
        return features_.count(feature) > 0;
    }
    
    void add_feature(const std::string& feature) {
        features_.insert(feature);
    }
    
    bool supports_simd() const {
        return simd_.has_vector();
    }
    
    bool supports_avx() const { return simd_.avx; }
    bool supports_avx2() const { return simd_.avx2; }
    bool supports_avx512() const { return simd_.avx512f; }
    bool supports_neon() const { return simd_.neon; }
    bool supports_sve() const { return simd_.sve; }
    
    bool is_64bit() const { return pointer_size_ == 64; }
    bool is_little_endian() const { return endianness_ == "little"; }
    
    size_t get_pointer_size() const { return pointer_size_; }
    size_t get_native_vector_width() const { return simd_.get_vector_width(); }
    
    std::string get_data_layout() const;
    std::string get_target_triple() const { return triple_; }
    
    enum class OptLevel {
        O0,
        O1,
        O2,
        O3,
        Oz,
        Os
    };
    
    void set_opt_level(OptLevel level) { opt_level_ = level; }
    OptLevel get_opt_level() const { return opt_level_; }
    
    void enable_vectorization(bool enable = true) { vectorize_ = enable; }
    bool vectorization_enabled() const { return vectorize_; }
    
    void enable_loop_unrolling(bool enable = true) { unroll_ = enable; }
    bool loop_unrolling_enabled() const { return unroll_; }

private:
    Architecture arch_;
    OS os_;
    ABI abi_;
    std::string triple_;
    std::string cpu_name_;
    CPUMicroarchitecture microarch_;
    SIMDInfo simd_;
    CacheInfo cache_;
    std::unordered_set<std::string> features_;
    size_t pointer_size_;
    std::string endianness_;
    OptLevel opt_level_;
    bool vectorize_;
    bool unroll_;
    
    void detect_cpu();
    void detect_features();
    void detect_cache();
    void parse_triple(const std::string& triple);
};

inline TargetMachine::TargetMachine() 
    : arch_(Architecture::X86_64),
      os_(OS::macOS),
      abi_(ABI::SysV),
      triple_("x86_64-apple-darwin"),
      cpu_name_("generic"),
      pointer_size_(64),
      endianness_("little"),
      opt_level_(OptLevel::O2),
      vectorize_(true),
      unroll_(true) {
    detect_features();
}

inline TargetMachine::TargetMachine(const std::string& triple) 
    : triple_(triple),
      pointer_size_(64),
      opt_level_(OptLevel::O2),
      vectorize_(true),
      unroll_(true) {
    parse_triple(triple);
    detect_features();
}

inline TargetMachine TargetMachine::detect() {
    TargetMachine tm;
    tm.detect_cpu();
    tm.detect_cache();
    return tm;
}

inline void TargetMachine::detect_cpu() {
#if defined(__APPLE__) && defined(__MACH__)
    os_ = OS::macOS;
#elif defined(__linux__)
    os_ = OS::Linux;
#elif defined(_WIN32)
    os_ = OS::Windows;
#endif

#if defined(__x86_64__) || defined(_M_X64)
    arch_ = Architecture::X86_64;
    cpu_name_ = "x86_64";
    pointer_size_ = 64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    arch_ = Architecture::AArch64;
    cpu_name_ = "arm64";
    pointer_size_ = 64;
    simd_.neon = true;
#elif defined(__riscv) && __riscv_xlen == 64
    arch_ = Architecture::RISCV64;
    cpu_name_ = "riscv64";
    pointer_size_ = 64;
#elif defined(__arm__)
    arch_ = Architecture::ARM;
    cpu_name_ = "arm";
    pointer_size_ = 32;
#elif defined(__wasm__)
    arch_ = Architecture::WASM;
    cpu_name_ = "wasm";
    pointer_size_ = 32;
#else
    arch_ = Architecture::Unknown;
    cpu_name_ = "unknown";
#endif

#if defined(__APPLE__)
    if (arch_ == Architecture::X86_64) {
        abi_ = ABI::SysV;
    } else if (arch_ == Architecture::AArch64) {
        abi_ = ABI::NEON;
    }
#endif

#if defined(__FLOAT_WORD_ORDER__) && __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__
    endianness_ = "big";
#else
    endianness_ = "little";
#endif
}

inline void TargetMachine::detect_features() {
#if defined(__x86_64__) || defined(_M_X64)
    simd_.sse = true;
    simd_.sse2 = true;
    
#if defined(__SSE3__)
    simd_.sse3 = true;
#endif
#if defined(__SSSE3__)
    simd_.ssse3 = true;
#endif
#if defined(__SSE4_1__)
    simd_.sse4_1 = true;
#endif
#if defined(__SSE4_2__)
    simd_.sse4_2 = true;
#endif
#if defined(__AVX__)
    simd_.avx = true;
#endif
#if defined(__AVX2__)
    simd_.avx2 = true;
#endif
#if defined(__AVX512F__)
    simd_.avx512f = true;
#endif
#if defined(__AVX512BW__)
    simd_.avx512bw = true;
#endif
#if defined(__AVX512DQ__)
    simd_.avx512dq = true;
#endif

    features_.insert("sse");
    features_.insert("sse2");
    if (simd_.sse3) features_.insert("sse3");
    if (simd_.ssse3) features_.insert("ssse3");
    if (simd_.sse4_1) features_.insert("sse4.1");
    if (simd_.sse4_2) features_.insert("sse4.2");
    if (simd_.avx) features_.insert("avx");
    if (simd_.avx2) features_.insert("avx2");
    if (simd_.avx512f) features_.insert("avx512f");

#elif defined(__aarch64__) || defined(_M_ARM64)
    simd_.neon = true;
    features_.insert("neon");
    
#if defined(__ARM_FEATURE_SVE)
    simd_.sve = true;
    features_.insert("sve");
#endif

#elif defined(__arm__) || defined(_M_ARM)
    simd_.neon = true;
    features_.insert("neon");
#endif
}

inline void TargetMachine::detect_cache() {
#if defined(__APPLE__)
    cache_.l1d_size = 32 * 1024;
    cache_.l1i_size = 32 * 1024;
    cache_.l2_size = 256 * 1024;
    cache_.line_size = 64;
#elif defined(__linux__)
    cache_.l1d_size = 32 * 1024;
    cache_.l1i_size = 32 * 1024;
    cache_.l2_size = 256 * 1024;
    cache_.line_size = 64;
#endif
}

inline void TargetMachine::parse_triple(const std::string& triple) {
    auto parts = std::vector<std::string>{};
    auto current = std::string{};
    for (char c : triple) {
        if (c == '-') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    
    if (parts.size() >= 1) {
        if (parts[0] == "x86_64" || parts[0] == "i386" || parts[0] == "i686") {
            arch_ = Architecture::X86_64;
        } else if (parts[0] == "aarch64" || parts[0] == "arm64") {
            arch_ = Architecture::AArch64;
        } else if (parts[0] == "arm") {
            arch_ = Architecture::ARM;
        } else if (parts[0] == "riscv64") {
            arch_ = Architecture::RISCV64;
        } else if (parts[0] == "wasm32" || parts[0] == "wasm64") {
            arch_ = Architecture::WASM;
        }
    }
    
    if (parts.size() >= 2) {
        if (parts[1] == "darwin" || parts[1] == "macosx") {
            os_ = OS::macOS;
        } else if (parts[1] == "linux") {
            os_ = OS::Linux;
        } else if (parts[1] == "windows") {
            os_ = OS::Windows;
        }
    }
}

inline std::string TargetMachine::get_data_layout() const {
    std::string layout;
    
    layout += "e";
    layout += "-";
    layout += endianness_ == "little" ? "p" : "P";
    layout += std::to_string(pointer_size_) + ":";
    layout += "i" + std::to_string(pointer_size_) + ":";
    layout += "i32:32:";
    layout += "i64:64:";
    layout += "i128:128:";
    
    if (simd_.avx512f) {
        layout += "v512:512:";
        layout += "v64:64:";
    }
    if (simd_.avx2 || simd_.avx) {
        layout += "v256:256:";
        layout += "v32:32:";
    }
    if (simd_.neon || simd_.sse) {
        layout += "v128:128:";
        layout += "v16:16:";
    }
    
    layout += "f32:32:";
    layout += "f64:64:";
    layout += "f128:128:";
    layout += "i1:8:8:";
    layout += "i8:8:8:";
    layout += "i16:16:16:";
    layout += "i32:32:32:";
    layout += "i64:64:64:";
    
    layout += "-";
    layout += "n" + std::to_string(pointer_size_);
    
    if (cache_.l1d_size > 0) {
        layout += ":n" + std::to_string(cache_.l1d_size);
    }
    
    return layout;
}

class TargetSelector {
public:
    struct SelectResult {
        TargetMachine machine;
        bool valid;
    };
    
    static SelectResult select(const std::string& target_spec);
    
    static TargetMachine host() {
        return TargetMachine::detect();
    }
    
    static TargetMachine generic_x86_64() {
        TargetMachine tm;
        tm.add_feature("sse");
        tm.add_feature("sse2");
        return tm;
    }
    
    static TargetMachine generic_aarch64() {
        TargetMachine tm;
        tm.add_feature("neon");
        return tm;
    }
};

inline TargetSelector::SelectResult TargetSelector::select(const std::string& target_spec) {
    SelectResult result;
    result.valid = false;
    
    if (target_spec == "host") {
        result.machine = host();
        result.valid = true;
    } else if (target_spec == "x86-64" || target_spec == "x86_64" || target_spec == "x64") {
        result.machine = generic_x86_64();
        result.valid = true;
    } else if (target_spec == "aarch64" || target_spec == "arm64") {
        result.machine = generic_aarch64();
        result.valid = true;
    } else if (target_spec.find('-') != std::string::npos) {
        result.machine = TargetMachine(target_spec);
        result.valid = true;
    }
    
    return result;
}

class InstructionSelector {
public:
    explicit InstructionSelector(const TargetMachine& target) 
        : target_(target) {}
    
    struct Selection {
        std::string instruction;
        std::vector<std::string> operands;
    };
    
    Selection select_load(const std::string& ty, const std::string& addr) const;
    Selection select_store(const std::string& ty, const std::string& val, const std::string& addr) const;
    Selection select_add(const std::string& ty, const std::string& lhs, const std::string& rhs) const;
    Selection select_mul(const std::string& ty, const std::string& lhs, const std::string& rhs) const;
    Selection select_call(const std::string& func, const std::vector<std::string>& args) const;
    
private:
    const TargetMachine& target_;
};

inline InstructionSelector::Selection InstructionSelector::select_load(
    const std::string& ty, const std::string& addr) const {
    Selection sel;
    
    if (target_.supports_avx()) {
        if (ty == "float") {
            sel.instruction = "vmovss";
        } else if (ty == "double") {
            sel.instruction = "vmovsd";
        } else {
            sel.instruction = "vmovdqu";
        }
    } else {
        if (ty == "float") {
            sel.instruction = "movss";
        } else if (ty == "double") {
            sel.instruction = "movsd";
        } else {
            sel.instruction = "movdqu";
        }
    }
    
    sel.operands = {"%" + ty, addr};
    return sel;
}

inline InstructionSelector::Selection InstructionSelector::select_store(
    const std::string& ty, const std::string& val, const std::string& addr) const {
    Selection sel;
    
    if (target_.supports_avx()) {
        if (ty == "float") {
            sel.instruction = "vmovss";
        } else if (ty == "double") {
            sel.instruction = "vmovsd";
        } else {
            sel.instruction = "vmovdqu";
        }
    } else {
        if (ty == "float") {
            sel.instruction = "movss";
        } else if (ty == "double") {
            sel.instruction = "movsd";
        } else {
            sel.instruction = "movdqu";
        }
    }
    
    sel.operands = {val, addr};
    return sel;
}

inline InstructionSelector::Selection InstructionSelector::select_add(
    const std::string& ty, const std::string& lhs, const std::string& rhs) const {
    Selection sel;
    
    if (target_.supports_avx()) {
        sel.instruction = "vadd" + ty;
    } else {
        sel.instruction = "add" + ty;
    }
    
    sel.operands = {lhs, rhs, "%" + ty};
    return sel;
}

inline InstructionSelector::Selection InstructionSelector::select_mul(
    const std::string& ty, const std::string& lhs, const std::string& rhs) const {
    Selection sel;
    
    if (target_.supports_avx()) {
        sel.instruction = "vmul" + ty;
    } else {
        sel.instruction = "mul" + ty;
    }
    
    sel.operands = {lhs, rhs, "%" + ty};
    return sel;
}

inline InstructionSelector::Selection InstructionSelector::select_call(
    const std::string& func, const std::vector<std::string>& args) const {
    Selection sel;
    sel.instruction = "call";
    sel.operands.push_back(func);
    for (const auto& arg : args) {
        sel.operands.push_back(arg);
    }
    return sel;
}

}

#endif
