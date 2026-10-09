/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BackendTest.h"
#include "Skip.h"

#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanFencePool.h"

#include <backend/platforms/VulkanPlatform.h>

#include <bluevk/BlueVK.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace test {

using namespace filament::backend;

namespace {

constexpr size_t FENCE_COUNT = 4;

// Counts the fences created and destroyed on the test's thread, which is where the pool under test
// runs; the driver's own thread is left out.
struct FenceCounter {
    static inline std::thread::id sThread;
    static inline size_t sCreated = 0;
    static inline size_t sDestroyed = 0;
    static inline PFN_vkCreateFence sCreateFence = nullptr;
    static inline PFN_vkDestroyFence sDestroyFence = nullptr;

    FenceCounter() {
        sThread = std::this_thread::get_id();
        sCreated = 0;
        sDestroyed = 0;
        sCreateFence = bluevk::vkCreateFence;
        sDestroyFence = bluevk::vkDestroyFence;
        bluevk::vkCreateFence = createFence;
        bluevk::vkDestroyFence = destroyFence;
    }

    ~FenceCounter() {
        bluevk::vkCreateFence = sCreateFence;
        bluevk::vkDestroyFence = sDestroyFence;
    }

    static VkResult VKAPI_CALL createFence(VkDevice device, VkFenceCreateInfo const* createInfo,
            VkAllocationCallbacks const* allocator, VkFence* fence) {
        if (std::this_thread::get_id() == sThread) {
            sCreated++;
        }
        return sCreateFence(device, createInfo, allocator, fence);
    }

    static void VKAPI_CALL destroyFence(VkDevice device, VkFence fence,
            VkAllocationCallbacks const* allocator) {
        if (std::this_thread::get_id() == sThread) {
            sDestroyed++;
        }
        sDestroyFence(device, fence, allocator);
    }
};

// Takes FENCE_COUNT fences from the pool at once, then gives them all back.
void cycleFences(VulkanFencePool& pool) {
    std::vector<std::shared_ptr<VulkanCmdFence>> statuses;
    for (size_t i = 0; i < FENCE_COUNT; i++) {
        statuses.push_back(pool.acquireFenceStatus());
    }
}

} // namespace

class VulkanFencePoolTest : public BackendTest {
protected:
    VkDevice getDevice() { return static_cast<VulkanPlatform*>(getPlatform())->getDevice(); }

    VulkanContext mContext;
};

TEST_F(VulkanFencePoolTest, KeepsIdleFencesAcrossFrames) {
    SKIP_IF_NOT(Backend::VULKAN, "The fence pool is part of the Vulkan backend");

    FenceCounter const counter;
    VulkanFencePool pool(mContext, getDevice(), 0);
    cycleFences(pool);
    // gc() runs about twice per frame, and every renderStandaloneView() is a frame.
    for (int i = 0; i < 20; i++) {
        pool.gc();
    }
    cycleFences(pool);
    EXPECT_EQ(FenceCounter::sCreated, FENCE_COUNT);
    EXPECT_EQ(FenceCounter::sDestroyed, 0);
    pool.terminate();
}

TEST_F(VulkanFencePoolTest, DestroysFencesIdleForASecond) {
    SKIP_IF_NOT(Backend::VULKAN, "The fence pool is part of the Vulkan backend");

    FenceCounter const counter;
    VulkanFencePool pool(mContext, getDevice(), 0);
    cycleFences(pool);
    pool.gc();
    EXPECT_EQ(FenceCounter::sDestroyed, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    pool.gc();
    EXPECT_EQ(FenceCounter::sDestroyed, FENCE_COUNT);
    pool.terminate();
}

} // namespace test
