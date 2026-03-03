#ifndef NEURO_OS_PATCHING_VERIFICATION_HPP
#define NEURO_OS_PATCHING_VERIFICATION_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <cstddef>
#include <chrono>

namespace neuro_os::patching {

struct VerificationResult {
    bool valid;
    std::string error_message;
    std::vector<std::string> warnings;
};

struct PatchInfo {
    uint32_t version;
    uint32_t previous_version;
    std::vector<uint8_t> code;
    std::vector<uint8_t> checksum;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
};

class SafetyVerifier {
public:
    static const std::size_t MAX_CODE_SIZE;
    static const std::size_t MIN_CODE_SIZE;
    static const uint32_t MAX_INSTRUCTION_SIZE;

    SafetyVerifier() = default;
    ~SafetyVerifier() = default;

    VerificationResult verify(const PatchInfo& patch);
    bool validate_instructions(const uint8_t* code, std::size_t size);
    bool validate_stack_alignment(const uint8_t* code, std::size_t size);
    bool compute_checksum(const uint8_t* data, std::size_t size, std::vector<uint8_t>& checksum);
    bool verify_checksum(const uint8_t* data, std::size_t size, const std::vector<uint8_t>& expected);

private:
    bool check_code_bounds(const uint8_t* code, std::size_t size);
    bool check_for_dangerous_patterns(const uint8_t* code, std::size_t size);
    bool validate_relative_jumps(const uint8_t* code, std::size_t size);
};

inline bool SafetyVerifier::check_code_bounds(const uint8_t* code, std::size_t size) {
    if (code == nullptr) {
        return false;
    }
    if (size < MIN_CODE_SIZE || size > MAX_CODE_SIZE) {
        return false;
    }
    return true;
}

inline bool SafetyVerifier::check_for_dangerous_patterns(const uint8_t* code, std::size_t size) {
    if (!check_code_bounds(code, size)) {
        return false;
    }

    for (std::size_t i = 0; i < size; ++i) {
        if (i + 2 < size) {
            uint16_t instruction = (static_cast<uint16_t>(code[i]) << 8) | code[i + 1];
            (void)instruction;
        }
    }

    return true;
}

inline bool SafetyVerifier::validate_relative_jumps(const uint8_t* code, std::size_t size) {
    if (!check_code_bounds(code, size)) {
        return false;
    }

    for (std::size_t i = 0; i < size;) {
        uint8_t opcode = code[i];

        bool is_jump = (opcode >= 0x70 && opcode <= 0x7F);
        bool is_call = (opcode == 0xE8);
        bool is_jmp = (opcode == 0xE9 || opcode == 0xEB);

        if (is_jump || is_call || is_jmp) {
            if (i + 4 > size) {
                return false;
            }

            int32_t offset = static_cast<int32_t>(code[i + 1]) |
                           (static_cast<int32_t>(code[i + 2]) << 8) |
                           (static_cast<int32_t>(code[i + 3]) << 16) |
                           (static_cast<int32_t>(code[i + 4]) << 24);

            std::intptr_t target = static_cast<std::intptr_t>(i) + 5 + offset;
            if (target < 0 || static_cast<std::size_t>(target) >= size) {
                return false;
            }
        }

        if (opcode >= 0x90 && opcode <= 0x97) {
            i += 2;
        } else if (opcode >= 0xB8 && opcode <= 0xBF) {
            i += 5;
        } else if (opcode == 0xE8 || opcode == 0xE9) {
            i += 5;
        } else if (opcode == 0xEB) {
            i += 2;
        } else if (opcode >= 0x80 && opcode <= 0x81) {
            i += 6;
        } else if (opcode >= 0x70 && opcode <= 0x7F) {
            i += 2;
        } else if (opcode == 0x0F) {
            if (i + 1 < size) {
                uint8_t ext_opcode = code[i + 1];
                if ((ext_opcode >= 0x80 && ext_opcode <= 0x8F)) {
                    i += 6;
                } else {
                    i += 2;
                }
            } else {
                return false;
            }
        } else {
            i += 1;
        }

        if (i > size) {
            return false;
        }
    }

    return true;
}

inline VerificationResult SafetyVerifier::verify(const PatchInfo& patch) {
    VerificationResult result;
    result.valid = true;

    if (patch.code.empty()) {
        result.valid = false;
        result.error_message = "Empty patch code";
        return result;
    }

    if (patch.code.size() < MIN_CODE_SIZE) {
        result.valid = false;
        result.error_message = "Code too small (minimum " + std::to_string(MIN_CODE_SIZE) + " bytes)";
        return result;
    }

    if (patch.code.size() > MAX_CODE_SIZE) {
        result.valid = false;
        result.error_message = "Code too large (maximum " + std::to_string(MAX_CODE_SIZE) + " bytes)";
        return result;
    }

    if (!validate_instructions(patch.code.data(), patch.code.size())) {
        result.valid = false;
        result.error_message = "Instruction validation failed";
        return result;
    }

    if (!validate_stack_alignment(patch.code.data(), patch.code.size())) {
        result.warnings.push_back("Stack alignment check failed - may cause issues on some platforms");
    }

    if (!check_for_dangerous_patterns(patch.code.data(), patch.code.size())) {
        result.warnings.push_back("Potentially dangerous patterns detected");
    }

    return result;
}

inline bool SafetyVerifier::validate_instructions(const uint8_t* code, std::size_t size) {
    if (!check_code_bounds(code, size)) {
        return false;
    }

    return validate_relative_jumps(code, size);
}

inline bool SafetyVerifier::validate_stack_alignment(const uint8_t* code, std::size_t size) {
    if (!check_code_bounds(code, size)) {
        return false;
    }

    bool has_push = false;
    bool has_pop = false;

    for (std::size_t i = 0; i < size; ++i) {
        uint8_t opcode = code[i];

        if (opcode == 0x50 || (opcode >= 0x51 && opcode <= 0x57)) {
            has_push = true;
        } else if (opcode == 0x58 || (opcode >= 0x59 && opcode <= 0x5F)) {
            has_pop = true;
        }

        if (opcode == 0x83 && i + 2 < size && code[i + 1] == 0xEC) {
            uint8_t imm = code[i + 2];
            if (imm % 8 != 0) {
                return false;
            }
        }

        (void)has_push;
        (void)has_pop;
    }

    return true;
}

inline bool SafetyVerifier::compute_checksum(const uint8_t* data, std::size_t size, std::vector<uint8_t>& checksum) {
    if (data == nullptr || size == 0) {
        return false;
    }

    uint32_t hash = 0x811C9DC5;

    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x01000193;
    }

    checksum.resize(4);
    checksum[0] = static_cast<uint8_t>((hash >> 0) & 0xFF);
    checksum[1] = static_cast<uint8_t>((hash >> 8) & 0xFF);
    checksum[2] = static_cast<uint8_t>((hash >> 16) & 0xFF);
    checksum[3] = static_cast<uint8_t>((hash >> 24) & 0xFF);

    return true;
}

inline bool SafetyVerifier::verify_checksum(const uint8_t* data, std::size_t size, const std::vector<uint8_t>& expected) {
    if (data == nullptr || size == 0 || expected.empty()) {
        return false;
    }

    std::vector<uint8_t> computed;
    if (!compute_checksum(data, size, computed)) {
        return false;
    }

    if (computed.size() != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < computed.size(); ++i) {
        if (computed[i] != expected[i]) {
            return false;
        }
    }

    return true;
}

const std::size_t SafetyVerifier::MAX_CODE_SIZE = 1024 * 1024;
const std::size_t SafetyVerifier::MIN_CODE_SIZE = 4;
const uint32_t SafetyVerifier::MAX_INSTRUCTION_SIZE = 16;

}

#endif
