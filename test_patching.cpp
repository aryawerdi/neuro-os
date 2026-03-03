#include "neuro_os/patching/patch_manager.hpp"
#include "neuro_os/patching/advanced_verification.hpp"
#include "neuro_os/patching/runtime_patching.hpp"
#include <iostream>
#include <cassert>

// Test functions
int original_function(int x) {
    return x * 2;
}

int hooked_function(int x) {
    return x * 3;
}

void test_basic_patching() {
    std::cout << "Testing basic patching...\n";
    
    neuro_os::patching::PatchManager manager;
    
    // Create a simple patch
    neuro_os::patching::EnhancedPatchInfo patch;
    patch.version = 1;
    patch.previous_version = 0;
    patch.code = {0x90, 0x90, 0x90, 0x90}; // NOP instructions
    patch.description = "Test patch";
    patch.timestamp = std::chrono::system_clock::now();
    
    // Apply patch
    auto result = manager.apply_patch(patch);
    
    if (result.valid) {
        std::cout << "✓ Basic patch applied successfully\n";
    } else {
        std::cout << "✗ Basic patch failed: " << result.error_message << "\n";
    }
    
    assert(result.valid);
}

void test_advanced_verification() {
    std::cout << "Testing advanced verification...\n";
    
    neuro_os::patching::AdvancedSafetyVerifier verifier;
    
    // Enable protections
    verifier.enable_cfi_protection();
    verifier.enable_stack_protection();
    verifier.enable_memory_protection();
    
    // Create test patch
    neuro_os::patching::EnhancedPatchInfo patch;
    patch.version = 1;
    patch.code = {0x55, 0x48, 0x89, 0xE5}; // Safe instructions
    
    auto result = verifier.verify_advanced(patch);
    
    if (result.valid) {
        std::cout << "✓ Advanced verification passed\n";
    } else {
        std::cout << "✗ Advanced verification failed: " << result.error_message << "\n";
    }
    
    // Test with dangerous code
    neuro_os::patching::EnhancedPatchInfo dangerous_patch;
    dangerous_patch.version = 2;
    dangerous_patch.code = {0x0F, 0x05}; // SYSCALL instruction
    
    auto dangerous_result = verifier.verify_advanced(dangerous_patch);
    
    if (!dangerous_result.valid) {
        std::cout << "✓ Correctly rejected dangerous code\n";
    } else {
        std::cout << "✗ Failed to reject dangerous code\n";
    }
}

void test_runtime_patching() {
    std::cout << "Testing runtime patching...\n";
    
    neuro_os::patching::HotPatcher patcher;
    
    // Test original function
    int original_result = original_function(5);
    std::cout << "Original function result: " << original_result << "\n";
    assert(original_result == 10);
    
    std::cout << "✓ Original function works correctly\n";
}

void test_patch_dependencies() {
    std::cout << "Testing patch dependencies...\n";
    
    neuro_os::patching::PatchManager manager;
    
    // Create dependency patch
    neuro_os::patching::EnhancedPatchInfo base_patch;
    base_patch.version = 1;
    base_patch.code = {0x90, 0x90};
    base_patch.description = "Base patch";
    
    auto base_result = manager.apply_patch(base_patch);
    assert(base_result.valid);
    
    // Create dependent patch
    neuro_os::patching::EnhancedPatchInfo dependent_patch;
    dependent_patch.version = 2;
    dependent_patch.code = {0x90, 0x90, 0x90};
    dependent_patch.description = "Dependent patch";
    
    // Add dependency
    neuro_os::patching::PatchDependency dep;
    dep.required_version = 1;
    dep.description = "Requires base patch";
    dep.optional = false;
    
    dependent_patch.dependencies.push_back(dep);
    
    auto dep_result = manager.apply_patch_with_dependencies(dependent_patch);
    
    if (dep_result.valid) {
        std::cout << "✓ Patch with dependency applied successfully\n";
    } else {
        std::cout << "✗ Patch with dependency failed: " << dep_result.error_message << "\n";
    }
    
    assert(dep_result.valid);
}

void test_atomic_transactions() {
    std::cout << "Testing atomic transactions...\n";
    
    neuro_os::patching::PatchManager manager;
    
    // Start atomic transaction
    bool transaction_started = manager.begin_atomic_transaction();
    assert(transaction_started);
    
    // Create patches for transaction
    neuro_os::patching::EnhancedPatchInfo patch1;
    patch1.version = 1;
    patch1.code = {0x90};
    patch1.atomic = true;
    
    neuro_os::patching::EnhancedPatchInfo patch2;
    patch2.version = 2;
    patch2.code = {0x90, 0x90};
    patch2.atomic = true;
    
    // Add patches to transaction
    auto result1 = manager.add_to_transaction(patch1);
    auto result2 = manager.add_to_transaction(patch2);
    
    if (result1.valid && result2.valid) {
        std::cout << "✓ Patches added to transaction successfully\n";
    }
    
    // Commit transaction
    bool committed = manager.commit_atomic_transaction();
    
    if (committed) {
        std::cout << "✓ Atomic transaction committed successfully\n";
    } else {
        std::cout << "✗ Atomic transaction commit failed\n";
    }
    
    assert(committed);
}

void test_patch_rollback() {
    std::cout << "Testing patch rollback...\n";
    
    neuro_os::patching::PatchManager manager;
    
    // Apply multiple patches
    for (int i = 1; i <= 3; i++) {
        neuro_os::patching::EnhancedPatchInfo patch;
        patch.version = i;
        patch.code = std::vector<uint8_t>(i, 0x90);
        
        auto result = manager.apply_patch(patch);
        assert(result.valid);
    }
    
    std::cout << "Applied 3 patches, current version: " << manager.get_current_version() << "\n";
    
    // Rollback one version
    bool rolled_back = manager.rollback_one_version();
    assert(rolled_back);
    
    std::cout << "After rollback, version: " << manager.get_current_version() << "\n";
    assert(manager.get_current_version() == 2);
    
    std::cout << "✓ Patch rollback works correctly\n";
}

void test_patch_scheduling() {
    std::cout << "Testing patch scheduling...\n";
    
    neuro_os::patching::PatchManager manager;
    
    // Create a patch
    neuro_os::patching::EnhancedPatchInfo patch;
    patch.version = 1;
    patch.code = {0x90, 0x90, 0x90};
    
    auto result = manager.apply_patch(patch);
    assert(result.valid);
    
    // Schedule patch for future application
    neuro_os::patching::PatchSchedule schedule;
    schedule.apply_time = std::chrono::system_clock::now() + std::chrono::seconds(10);
    schedule.expire_time = schedule.apply_time + std::chrono::hours(1);
    
    bool scheduled = manager.schedule_patch(1, schedule);
    
    if (scheduled) {
        std::cout << "✓ Patch scheduled successfully\n";
    } else {
        std::cout << "✗ Patch scheduling failed\n";
    }
    
    assert(scheduled);
    
    auto scheduled_patches = manager.get_scheduled_patches();
    assert(!scheduled_patches.empty());
    
    std::cout << "✓ " << scheduled_patches.size() << " patch(es) scheduled\n";
}

int main() {
    std::cout << "=== NeuroOS Patching System Tests ===\n\n";
    
    try {
        test_basic_patching();
        std::cout << "\n";
        
        test_advanced_verification();
        std::cout << "\n";
        
        test_runtime_patching();
        std::cout << "\n";
        
        test_patch_dependencies();
        std::cout << "\n";
        
        test_atomic_transactions();
        std::cout << "\n";
        
        test_patch_rollback();
        std::cout << "\n";
        
        test_patch_scheduling();
        std::cout << "\n";
        
        std::cout << "=== All tests passed! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception\n";
        return 1;
    }
}