#ifndef NEURO_OS_PATCHING_PATCH_MANAGER_HPP
#define NEURO_OS_PATCHING_PATCH_MANAGER_HPP

#include "verification.hpp"
#include "code_loader.hpp"
#include "patch_types.hpp"
#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <thread>
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
    std::map<uint32_t, EnhancedPatchInfo> patch_history_;
    std::map<uint32_t, std::vector<uint8_t> > patch_states_;
    uint32_t current_version_ = 0;
    std::vector<uint8_t> current_code_;
    std::unique_ptr<SafetyVerifier> verifier_;
    std::function<std::vector<uint8_t>(const std::string&)> code_load_callback_;
    std::function<bool(const std::vector<uint8_t>&, uintptr_t)> apply_callback_;
    
    std::mutex patch_mutex_;
    std::condition_variable patch_cv_;
    std::atomic<bool> atomic_operation_in_progress_{false};
    std::queue<AtomicTransaction> pending_transactions_;
    std::unordered_set<uint32_t> applied_patches_;
    
    std::thread scheduler_thread_;
    std::atomic<bool> scheduler_running_{true};
    std::map<uint32_t, PatchSchedule> scheduled_patches_;
    
    bool compute_checksum(const std::vector<uint8_t>& code, std::vector<uint8_t>& checksum);
    bool verify_checksum(const std::vector<uint8_t>& code, const std::vector<uint8_t>& expected_checksum);
    
    bool check_dependencies(const EnhancedPatchInfo& patch);
    bool create_backup_state(std::vector<uint8_t>& backup);
    bool restore_backup_state(const std::vector<uint8_t>& backup);
    
    VerificationResult apply_patch_atomic(const EnhancedPatchInfo& patch);
    VerificationResult apply_patch_non_atomic(const EnhancedPatchInfo& patch);
    
    void scheduler_loop();
    void check_scheduled_patches();
    
    bool validate_patch_metadata(const EnhancedPatchInfo& patch);
    bool verify_patch_signature(const EnhancedPatchInfo& patch);

public:
    PatchManager();
    ~PatchManager();
    
    VerificationResult apply_patch(const EnhancedPatchInfo& patch);
    VerificationResult apply_patch_from_file(const std::string& patch_path);
    VerificationResult apply_patch_with_dependencies(const EnhancedPatchInfo& patch);
    
    bool begin_atomic_transaction();
    bool commit_atomic_transaction();
    bool rollback_atomic_transaction();
    VerificationResult add_to_transaction(const EnhancedPatchInfo& patch);
    
    bool rollback(uint32_t target_version);
    bool rollback_one_version();
    bool rollback_with_state_restoration(uint32_t target_version);
    
    bool schedule_patch(uint32_t patch_version, const PatchSchedule& schedule);
    bool cancel_scheduled_patch(uint32_t patch_version);
    std::vector<uint32_t> get_scheduled_patches() const;
    
    bool add_dependency(uint32_t patch_version, const PatchDependency& dependency);
    bool remove_dependency(uint32_t patch_version, uint32_t required_version);
    std::vector<PatchDependency> get_dependencies(uint32_t patch_version) const;
    
    bool set_patch_priority(uint32_t patch_version, uint32_t priority);
    uint32_t get_patch_priority(uint32_t patch_version) const;
    
    bool set_patch_metadata(uint32_t patch_version, const std::string& key, const std::string& value);
    std::string get_patch_metadata(uint32_t patch_version, const std::string& key) const;
    
    uint32_t get_current_version() const;
    std::vector<uint32_t> get_available_versions() const;
    const EnhancedPatchInfo* get_patch_info(uint32_t version) const;
    const std::vector<uint8_t>& get_current_code() const;
    
    bool has_patches() const;
    size_t get_patch_count() const;
    bool clear_history();
    
    bool is_patch_applied(uint32_t version) const;
    std::vector<uint32_t> get_applied_patches() const;
    
    void set_code_loader(std::function<std::vector<uint8_t>(const std::string&)> loader);
    void set_apply_callback(std::function<bool(const std::vector<uint8_t>&, uintptr_t)> callback);
    
    bool wait_for_patch_application(uint32_t version, std::chrono::milliseconds timeout);
    bool is_atomic_operation_in_progress() const;
};



}

#endif
