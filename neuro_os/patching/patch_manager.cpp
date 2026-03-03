#include "patch_manager.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace neuro_os::patching {

PatchManager::PatchManager() 
    : verifier_(new SafetyVerifier()) {
    scheduler_thread_ = std::thread(&PatchManager::scheduler_loop, this);
}

PatchManager::~PatchManager() {
    scheduler_running_ = false;
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

bool PatchManager::check_dependencies(const EnhancedPatchInfo& patch) {
    for (const auto& dep : patch.dependencies) {
        if (patch_history_.find(dep.required_version) == patch_history_.end()) {
            if (!dep.optional) {
                return false;
            }
        }
    }
    return true;
}

bool PatchManager::create_backup_state(std::vector<uint8_t>& backup) {
    backup = current_code_;
    return true;
}

bool PatchManager::restore_backup_state(const std::vector<uint8_t>& backup) {
    current_code_ = backup;
    return true;
}

VerificationResult PatchManager::apply_patch_atomic(const EnhancedPatchInfo& patch) {
    VerificationResult result;
    
    std::vector<uint8_t> backup;
    if (!create_backup_state(backup)) {
        result.valid = false;
        result.error_message = "Failed to create backup state";
        return result;
    }
    
    result = verifier_->verify(patch);
    if (!result.valid) {
        return result;
    }
    
    if (!check_dependencies(patch)) {
        result.valid = false;
        result.error_message = "Patch dependencies not satisfied";
        return result;
    }
    
    std::vector<uint8_t> computed_checksum;
    if (!verifier_->compute_checksum(patch.code.data(), patch.code.size(), computed_checksum)) {
        result.valid = false;
        result.error_message = "Failed to compute checksum";
        return result;
    }
    
    if (computed_checksum != patch.checksum) {
        result.valid = false;
        result.error_message = "Checksum verification failed";
        return result;
    }
    
    current_code_ = patch.code;
    current_version_ = patch.version;
    patch_history_[patch.version] = patch;
    applied_patches_.insert(patch.version);
    
    if (apply_callback_) {
        if (!apply_callback_(current_code_, 0)) {
            result.warnings.push_back("Apply callback returned failure");
        }
    }
    
    return result;
}

VerificationResult PatchManager::apply_patch_non_atomic(const EnhancedPatchInfo& patch) {
    VerificationResult result = verifier_->verify(patch);
    
    if (!result.valid) {
        return result;
    }
    
    if (!check_dependencies(patch)) {
        result.valid = false;
        result.error_message = "Patch dependencies not satisfied";
        return result;
    }
    
    current_code_ = patch.code;
    current_version_ = patch.version;
    patch_history_[patch.version] = patch;
    applied_patches_.insert(patch.version);
    
    if (apply_callback_) {
        if (!apply_callback_(current_code_, 0)) {
            result.warnings.push_back("Apply callback returned failure");
        }
    }
    
    return result;
}

VerificationResult PatchManager::apply_patch(const EnhancedPatchInfo& patch) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    if (patch.atomic) {
        return apply_patch_atomic(patch);
    } else {
        return apply_patch_non_atomic(patch);
    }
}

VerificationResult PatchManager::apply_patch_from_file(const std::string& patch_path) {
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
    
    EnhancedPatchInfo patch;
    patch.version = current_version_ + 1;
    patch.previous_version = current_version_;
    patch.code = patch_data;
    patch.timestamp = std::chrono::system_clock::now();
    
    std::vector<uint8_t> checksum;
    if (verifier_->compute_checksum(patch.code.data(), patch.code.size(), checksum)) {
        patch.checksum = checksum;
    }
    
    return apply_patch(patch);
}

VerificationResult PatchManager::apply_patch_with_dependencies(const EnhancedPatchInfo& patch) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    VerificationResult result;
    
    for (const auto& dep : patch.dependencies) {
        if (!dep.optional) {
            auto it = patch_history_.find(dep.required_version);
            if (it == patch_history_.end()) {
                result.valid = false;
                result.error_message = "Required dependency not found: version " + 
                                      std::to_string(dep.required_version);
                return result;
            }
            
            if (applied_patches_.find(dep.required_version) == applied_patches_.end()) {
                VerificationResult dep_result = apply_patch(it->second);
                if (!dep_result.valid) {
                    result.valid = false;
                    result.error_message = "Failed to apply dependency: " + dep_result.error_message;
                    return result;
                }
            }
        }
    }
    
    return apply_patch(patch);
}

bool PatchManager::begin_atomic_transaction() {
    if (atomic_operation_in_progress_) {
        return false;
    }
    
    atomic_operation_in_progress_ = true;
    AtomicTransaction transaction;
    transaction.start_time = std::chrono::system_clock::now();
    
    if (!create_backup_state(transaction.backup_state)) {
        atomic_operation_in_progress_ = false;
        return false;
    }
    
    pending_transactions_.push(transaction);
    return true;
}

bool PatchManager::commit_atomic_transaction() {
    if (!atomic_operation_in_progress_ || pending_transactions_.empty()) {
        return false;
    }
    
    auto& transaction = pending_transactions_.front();
    transaction.committed = true;
    
    pending_transactions_.pop();
    atomic_operation_in_progress_ = false;
    
    return true;
}

bool PatchManager::rollback_atomic_transaction() {
    if (!atomic_operation_in_progress_ || pending_transactions_.empty()) {
        return false;
    }
    
    auto& transaction = pending_transactions_.front();
    
    if (!restore_backup_state(transaction.backup_state)) {
        return false;
    }
    
    transaction.rolled_back = true;
    pending_transactions_.pop();
    atomic_operation_in_progress_ = false;
    
    return true;
}

VerificationResult PatchManager::add_to_transaction(const EnhancedPatchInfo& patch) {
    VerificationResult result;
    
    if (!atomic_operation_in_progress_ || pending_transactions_.empty()) {
        result.valid = false;
        result.error_message = "No atomic transaction in progress";
        return result;
    }
    
    auto& transaction = pending_transactions_.front();
    
    result = verifier_->verify(patch);
    if (!result.valid) {
        return result;
    }
    
    transaction.patches.push_back(patch);
    return result;
}

bool PatchManager::rollback(uint32_t target_version) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(target_version);
    if (it == patch_history_.end()) {
        return false;
    }
    
    current_code_ = it->second.code;
    current_version_ = target_version;
    
    if (apply_callback_) {
        return apply_callback_(current_code_, 0);
    }
    
    return true;
}

bool PatchManager::rollback_one_version() {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
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

bool PatchManager::rollback_with_state_restoration(uint32_t target_version) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_states_.find(target_version);
    if (it == patch_states_.end()) {
        return rollback(target_version);
    }
    
    current_code_ = it->second;
    current_version_ = target_version;
    
    if (apply_callback_) {
        return apply_callback_(current_code_, 0);
    }
    
    return true;
}

bool PatchManager::schedule_patch(uint32_t patch_version, const PatchSchedule& schedule) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    scheduled_patches_[patch_version] = schedule;
    return true;
}

bool PatchManager::cancel_scheduled_patch(uint32_t patch_version) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    return scheduled_patches_.erase(patch_version) > 0;
}

std::vector<uint32_t> PatchManager::get_scheduled_patches() const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    std::vector<uint32_t> result;
    for (const auto& [version, _] : scheduled_patches_) {
        result.push_back(version);
    }
    return result;
}

bool PatchManager::add_dependency(uint32_t patch_version, const PatchDependency& dependency) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it == patch_history_.end()) {
        return false;
    }
    
    it->second.dependencies.push_back(dependency);
    return true;
}

bool PatchManager::remove_dependency(uint32_t patch_version, uint32_t required_version) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it == patch_history_.end()) {
        return false;
    }
    
    auto& deps = it->second.dependencies;
    deps.erase(std::remove_if(deps.begin(), deps.end(),
        [required_version](const PatchDependency& dep) {
            return dep.required_version == required_version;
        }), deps.end());
    
    return true;
}

std::vector<PatchDependency> PatchManager::get_dependencies(uint32_t patch_version) const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it != patch_history_.end()) {
        return it->second.dependencies;
    }
    
    return {};
}

bool PatchManager::set_patch_priority(uint32_t patch_version, uint32_t priority) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it == patch_history_.end()) {
        return false;
    }
    
    it->second.priority = priority;
    return true;
}

uint32_t PatchManager::get_patch_priority(uint32_t patch_version) const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it != patch_history_.end()) {
        return it->second.priority;
    }
    
    return 0;
}

bool PatchManager::set_patch_metadata(uint32_t patch_version, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it == patch_history_.end()) {
        return false;
    }
    
    it->second.metadata[key] = value;
    return true;
}

std::string PatchManager::get_patch_metadata(uint32_t patch_version, const std::string& key) const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(patch_version);
    if (it != patch_history_.end()) {
        auto meta_it = it->second.metadata.find(key);
        if (meta_it != it->second.metadata.end()) {
            return meta_it->second;
        }
    }
    
    return "";
}

uint32_t PatchManager::get_current_version() const {
    return current_version_;
}

std::vector<uint32_t> PatchManager::get_available_versions() const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    std::vector<uint32_t> versions;
    for (const auto& [version, _] : patch_history_) {
        versions.push_back(version);
    }
    return versions;
}

const EnhancedPatchInfo* PatchManager::get_patch_info(uint32_t version) const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto it = patch_history_.find(version);
    if (it != patch_history_.end()) {
        return &(it->second);
    }
    return nullptr;
}

const std::vector<uint8_t>& PatchManager::get_current_code() const {
    return current_code_;
}

bool PatchManager::has_patches() const {
    return current_version_ != 0;
}

size_t PatchManager::get_patch_count() const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    return patch_history_.size();
}

bool PatchManager::clear_history() {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    patch_history_.clear();
    patch_states_.clear();
    applied_patches_.clear();
    scheduled_patches_.clear();
    
    while (!pending_transactions_.empty()) {
        pending_transactions_.pop();
    }
    
    current_code_.clear();
    current_version_ = 0;
    atomic_operation_in_progress_ = false;
    
    return true;
}

bool PatchManager::is_patch_applied(uint32_t version) const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    return applied_patches_.find(version) != applied_patches_.end();
}

std::vector<uint32_t> PatchManager::get_applied_patches() const {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    std::vector<uint32_t> result;
    for (uint32_t version : applied_patches_) {
        result.push_back(version);
    }
    return result;
}

void PatchManager::set_code_loader(std::function<std::vector<uint8_t>(const std::string&)> loader) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    code_load_callback_ = std::move(loader);
}

void PatchManager::set_apply_callback(std::function<bool(const std::vector<uint8_t>&, uintptr_t)> callback) {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    apply_callback_ = std::move(callback);
}

bool PatchManager::wait_for_patch_application(uint32_t version, std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (is_patch_applied(version)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return false;
}

bool PatchManager::is_atomic_operation_in_progress() const {
    return atomic_operation_in_progress_;
}

void PatchManager::scheduler_loop() {
    while (scheduler_running_) {
        check_scheduled_patches();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void PatchManager::check_scheduled_patches() {
    std::lock_guard<std::mutex> lock(patch_mutex_);
    
    auto now = std::chrono::system_clock::now();
    
    for (auto it = scheduled_patches_.begin(); it != scheduled_patches_.end();) {
        const auto& schedule = it->second;
        
        if (now >= schedule.apply_time && now <= schedule.expire_time) {
            auto patch_it = patch_history_.find(it->first);
            if (patch_it != patch_history_.end()) {
                apply_patch(patch_it->second);
                
                if (schedule.recurring) {
                    auto new_schedule = schedule;
                    new_schedule.apply_time += schedule.recurrence_interval;
                    new_schedule.expire_time += schedule.recurrence_interval;
                    scheduled_patches_[it->first] = new_schedule;
                    ++it;
                } else {
                    it = scheduled_patches_.erase(it);
                }
            } else {
                ++it;
            }
        } else if (now > schedule.expire_time) {
            it = scheduled_patches_.erase(it);
        } else {
            ++it;
        }
    }
}

bool PatchManager::validate_patch_metadata(const EnhancedPatchInfo& patch) {
    if (patch.metadata.find("signature") == patch.metadata.end()) {
        return false;
    }
    
    if (patch.metadata.find("author") == patch.metadata.end()) {
        return false;
    }
    
    return true;
}

bool PatchManager::verify_patch_signature(const EnhancedPatchInfo& patch) {
    auto it = patch.metadata.find("signature");
    if (it == patch.metadata.end()) {
        return false;
    }
    
    return true;
}

} // namespace neuro_os::patching