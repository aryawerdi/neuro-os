#ifndef NEURO_OS_PATCHING_PATCH_MANAGER_HPP
#define NEURO_OS_PATCHING_PATCH_MANAGER_HPP

#include "verification.hpp"
#include "code_loader.hpp"
#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <functional>

#if defined(__APPLE__)
    #include <mach-o/loader.h>
    #include <mach-o/nlist.h>
#elif defined(__linux__)
    #include <elf.h>
#endif

namespace neuro_os::patching {

class PatchManager {
private:
    std::map<uint32_t, PatchInfo> patch_history_;
    uint32_t current_version_ = 0;
    std::vector<uint8_t> current_code_;
    std::unique_ptr<SafetyVerifier> verifier_;
    std::function<std::vector<uint8_t>(const std::string&)> code_load_callback_;
    std::function<bool(const std::vector<uint8_t>&, uintptr_t)> apply_callback_;

    bool compute_checksum(const std::vector<uint8_t>& code, std::vector<uint8_t>& checksum);
    bool verify_checksum(const std::vector<uint8_t>& code, const std::vector<uint8_t>& expected_checksum);

public:
    PatchManager();
    ~PatchManager() = default;

    VerificationResult apply_patch(const PatchInfo& patch);
    VerificationResult apply_patch_from_file(const std::string& patch_path);

    bool rollback(uint32_t target_version);
    bool rollback_one_version();

    uint32_t get_current_version() const;
    std::vector<uint32_t> get_available_versions() const;
    const PatchInfo* get_patch_info(uint32_t version) const;
    const std::vector<uint8_t>& get_current_code() const;

    bool has_patches() const;
    size_t get_patch_count() const;
    bool clear_history();

    void set_code_loader(std::function<std::vector<uint8_t>(const std::string&)> loader);
    void set_apply_callback(std::function<bool(const std::vector<uint8_t>&, uintptr_t)> callback);
};

inline PatchManager::PatchManager() 
    : verifier_(std::make_unique<SafetyVerifier>()) {
}

inline bool PatchManager::compute_checksum(const std::vector<uint8_t>& code, std::vector<uint8_t>& checksum) {
    if (code.empty()) {
        return false;
    }

    uint32_t hash = 0x811C9DC5;
    for (uint8_t byte : code) {
        hash ^= byte;
        hash *= 0x01000193;
    }

    checksum.resize(4);
    checksum[0] = (hash >> 0) & 0xFF;
    checksum[1] = (hash >> 8) & 0xFF;
    checksum[2] = (hash >> 16) & 0xFF;
    checksum[3] = (hash >> 24) & 0xFF;

    return true;
}

inline bool PatchManager::verify_checksum(const std::vector<uint8_t>& code, const std::vector<uint8_t>& expected_checksum) {
    std::vector<uint8_t> computed;
    if (!compute_checksum(code, computed)) {
        return false;
    }

    if (computed.size() != expected_checksum.size()) {
        return false;
    }

    for (size_t i = 0; i < computed.size(); ++i) {
        if (computed[i] != expected_checksum[i]) {
            return false;
        }
    }

    return true;
}

inline VerificationResult PatchManager::apply_patch(const PatchInfo& patch) {
    VerificationResult result;

    if (patch.code.empty()) {
        result.valid = false;
        result.error_message = "Patch code is empty";
        return result;
    }

    if (patch.version == 0) {
        result.valid = false;
        result.error_message = "Invalid patch version (0)";
        return result;
    }

    if (patch_history_.find(patch.version) != patch_history_.end()) {
        result.valid = false;
        result.error_message = "Patch version " + std::to_string(patch.version) + " already exists";
        return result;
    }

    result = verifier_->verify(patch);
    if (!result.valid) {
        return result;
    }

    if (!verify_checksum(patch.code, patch.checksum)) {
        result.valid = false;
        result.error_message = "Checksum verification failed";
        return result;
    }

    if (current_version_ != 0) {
        PatchInfo backup;
        backup.version = current_version_;
        backup.previous_version = 0;
        backup.code = current_code_;
        backup.description = "Backup before upgrade to v" + std::to_string(patch.version);
        backup.timestamp = std::chrono::system_clock::now();
        compute_checksum(backup.code, backup.checksum);
        patch_history_[current_version_] = backup;
    }

    current_code_ = patch.code;
    current_version_ = patch.version;
    patch_history_[patch.version] = patch;

    if (apply_callback_) {
        if (!apply_callback_(current_code_, 0)) {
            result.warnings.push_back("Apply callback returned failure");
        }
    }

    return result;
}

inline VerificationResult PatchManager::apply_patch_from_file(const std::string& patch_path) {
    VerificationResult result;

    std::vector<uint8_t> patch_data;
    if (code_load_callback_) {
        patch_data = code_load_callback_(patch_path);
    } else {
        CodeLoader loader;
        patch_data = loader.load_from_file(patch_path);
    }

    if (patch_data.empty()) {
        result.valid = false;
        result.error_message = "Failed to load patch from file: " + patch_path;
        return result;
    }

    PatchInfo patch;
    patch.version = current_version_ + 1;
    patch.previous_version = current_version_;
    patch.code = patch_data;
    patch.timestamp = std::chrono::system_clock::now();
    compute_checksum(patch.code, patch.checksum);

    return apply_patch(patch);
}

inline bool PatchManager::rollback(uint32_t target_version) {
    if (patch_history_.find(target_version) == patch_history_.end()) {
        return false;
    }

    const PatchInfo& target = patch_history_.at(target_version);
    current_code_ = target.code;
    current_version_ = target_version;

    if (apply_callback_) {
        return apply_callback_(current_code_, 0);
    }

    return true;
}

inline bool PatchManager::rollback_one_version() {
    if (current_version_ == 0) {
        return false;
    }

    auto it = patch_history_.find(current_version_);
    if (it == patch_history_.end()) {
        return false;
    }

    uint32_t prev_version = it->second.previous_version;
    if (prev_version == 0) {
        current_code_.clear();
        current_version_ = 0;
        return true;
    }

    return rollback(prev_version);
}

inline uint32_t PatchManager::get_current_version() const {
    return current_version_;
}

inline std::vector<uint32_t> PatchManager::get_available_versions() const {
    std::vector<uint32_t> versions;
    for (const auto& [version, _] : patch_history_) {
        versions.push_back(version);
    }
    return versions;
}

inline const PatchInfo* PatchManager::get_patch_info(uint32_t version) const {
    auto it = patch_history_.find(version);
    if (it != patch_history_.end()) {
        return &(it->second);
    }
    return nullptr;
}

inline const std::vector<uint8_t>& PatchManager::get_current_code() const {
    return current_code_;
}

inline bool PatchManager::has_patches() const {
    return current_version_ != 0;
}

inline size_t PatchManager::get_patch_count() const {
    return patch_history_.size();
}

inline bool PatchManager::clear_history() {
    patch_history_.clear();
    current_code_.clear();
    current_version_ = 0;
    return true;
}

inline void PatchManager::set_code_loader(std::function<std::vector<uint8_t>(const std::string&)> loader) {
    code_load_callback_ = std::move(loader);
}

inline void PatchManager::set_apply_callback(std::function<bool(const std::vector<uint8_t>&, uintptr_t)> callback) {
    apply_callback_ = std::move(callback);
}

}

#endif
