#ifndef NEURO_OS_PATCHING_PATCH_TYPES_HPP
#define NEURO_OS_PATCHING_PATCH_TYPES_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <map>

namespace neuro_os::patching {

struct PatchDependency {
    uint32_t required_version;
    std::string description;
    bool optional = false;
};

struct PatchSchedule {
    std::chrono::system_clock::time_point apply_time;
    std::chrono::system_clock::time_point expire_time;
    bool recurring = false;
    std::chrono::hours recurrence_interval;
};

struct EnhancedPatchInfo {
    uint32_t version;
    uint32_t previous_version;
    std::vector<uint8_t> code;
    std::vector<uint8_t> checksum;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    
    std::vector<PatchDependency> dependencies;
    PatchSchedule schedule;
    std::vector<uint8_t> rollback_data;
    bool atomic = true;
    uint32_t priority = 0;
    
    std::map<std::string, std::string> metadata;
    bool has_schedule = false;
};

struct AtomicTransaction {
    std::vector<EnhancedPatchInfo> patches;
    std::vector<uint8_t> backup_state;
    std::chrono::system_clock::time_point start_time;
    bool committed = false;
    bool rolled_back = false;
};

} // namespace neuro_os::patching

#endif // NEURO_OS_PATCHING_PATCH_TYPES_HPP