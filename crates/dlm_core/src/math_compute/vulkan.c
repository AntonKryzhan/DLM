#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

enum { DM_F32_MATRIX, DM_F64_MATRIX, DM_F32_VECTOR, DM_F64_VECTOR, DM_U32_NTT, DM_SHADER_COUNT };

typedef struct {
    uint32_t word[8];
    uint32_t x, y;
} DmDispatch;

typedef struct {
    const void *input;
    size_t input_bytes, bytes;
} DmTransfer;

/* 0 on unavailable, unsupported shape, or driver failure. Output stays private
 * until the whole submission and readback have completed. */
static bool dm_vulkan(
    unsigned shader, const DmDispatch *dispatches, size_t count,
    const DmTransfer transfers[4], unsigned output_slot, void *output, size_t output_bytes,
    uint64_t work
);

#if defined(_WIN32) || defined(__linux__)
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
typedef HMODULE DmLibrary;
static SRWLOCK dm_mutex = SRWLOCK_INIT;
static void dm_lock(void) { AcquireSRWLockExclusive(&dm_mutex); }
static void dm_unlock(void) { ReleaseSRWLockExclusive(&dm_mutex); }
static DmLibrary dm_open(void) { return LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); }
static PFN_vkVoidFunction dm_symbol(DmLibrary l, const char *name) { return (PFN_vkVoidFunction)GetProcAddress(l, name); }
static void dm_close(DmLibrary l) { FreeLibrary(l); }
#else
#include <dlfcn.h>
#include <pthread.h>
typedef void *DmLibrary;
static pthread_mutex_t dm_mutex = PTHREAD_MUTEX_INITIALIZER;
static void dm_lock(void) { pthread_mutex_lock(&dm_mutex); }
static void dm_unlock(void) { pthread_mutex_unlock(&dm_mutex); }
static DmLibrary dm_open(void) { return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL); }
static PFN_vkVoidFunction dm_symbol(DmLibrary l, const char *name) { return (PFN_vkVoidFunction)dlsym(l, name); }
static void dm_close(DmLibrary l) { dlclose(l); }
#endif

#define DM_VK_INSTANCE(X) \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceFeatures) \
    X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(CreateDevice)
#define DM_VK_DEVICE(X) \
    X(GetDeviceQueue) X(DeviceWaitIdle) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreateComputePipelines) X(DestroyPipeline) X(CreateCommandPool) X(DestroyCommandPool) \
    X(AllocateCommandBuffers) X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) X(QueueSubmit) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) \
    X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch) X(CmdPipelineBarrier) X(CmdCopyBuffer)

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkBuffer local;
    VkDeviceMemory local_memory;
    void *host;
    size_t capacity;
} DmBuffer;

typedef struct {
    DmLibrary library;
    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memory;
    VkDescriptorSetLayout descriptors;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipelines[DM_SHADER_COUNT];
    VkCommandPool pool;
    VkCommandBuffer command;
    VkFence fence;
    DmBuffer buffers[4];
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkDestroyDevice DestroyDevice;
#define DM_DECLARE(name) PFN_vk##name name;
    DM_VK_INSTANCE(DM_DECLARE)
    DM_VK_DEVICE(DM_DECLARE)
#undef DM_DECLARE
} DmDevice;

static DmDevice dm_device;
static bool dm_attempted;
static bool dm_ready;

static void dm_release_buffer(DmDevice *d, DmBuffer *buffer) {
    if (buffer->host) d->UnmapMemory(d->device, buffer->memory);
    if (buffer->buffer) d->DestroyBuffer(d->device, buffer->buffer, NULL);
    if (buffer->memory) d->FreeMemory(d->device, buffer->memory, NULL);
    if (buffer->local) d->DestroyBuffer(d->device, buffer->local, NULL);
    if (buffer->local_memory) d->FreeMemory(d->device, buffer->local_memory, NULL);
    *buffer = (DmBuffer){0};
}

static void dm_release(DmDevice *d) {
    if (d->device && d->DestroyDevice) {
        for (unsigned i = 0; i < 4; i++) dm_release_buffer(d, &d->buffers[i]);
        if (d->fence) d->DestroyFence(d->device, d->fence, NULL);
        if (d->pool) d->DestroyCommandPool(d->device, d->pool, NULL);
        for (unsigned i = 0; i < DM_SHADER_COUNT; i++)
            if (d->pipelines[i]) d->DestroyPipeline(d->device, d->pipelines[i], NULL);
        if (d->pipeline_layout) d->DestroyPipelineLayout(d->device, d->pipeline_layout, NULL);
        if (d->descriptor_pool) d->DestroyDescriptorPool(d->device, d->descriptor_pool, NULL);
        if (d->descriptors) d->DestroyDescriptorSetLayout(d->device, d->descriptors, NULL);
        d->DestroyDevice(d->device, NULL);
    }
    if (d->instance && d->DestroyInstance) d->DestroyInstance(d->instance, NULL);
    if (d->library) dm_close(d->library);
    *d = (DmDevice){0};
}

static void dm_exit(void) {
    dm_lock();
    dm_release(&dm_device);
    dm_ready = false;
    dm_unlock();
}

/* Adapt the dedicated-compute preference of AKENGINE's Vulkan RHI. */
static bool dm_queue_family(DmDevice *d, VkPhysicalDevice physical, uint32_t *chosen) {
    uint32_t count = 0;
    d->GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (!count) return false;
    VkQueueFamilyProperties *queues = calloc(count, sizeof(*queues));
    if (!queues) return false;
    d->GetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (!queues[i].queueCount || !(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
        if (best == UINT32_MAX) best = i;
        if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { best = i; break; }
    }
    free(queues);
    *chosen = best;
    return best != UINT32_MAX;
}

static VkPhysicalDevice dm_choose_device(DmDevice *d, uint32_t *family) {
    uint32_t count = 0;
    if (d->EnumeratePhysicalDevices(d->instance, &count, NULL) != VK_SUCCESS || !count) return VK_NULL_HANDLE;
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return VK_NULL_HANDLE;
    if (d->EnumeratePhysicalDevices(d->instance, &count, devices) != VK_SUCCESS) {
        free(devices);
        return VK_NULL_HANDLE;
    }
    VkPhysicalDevice best = VK_NULL_HANDLE;
    unsigned best_score = 0;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties p;
        VkPhysicalDeviceFeatures f;
        uint32_t queue;
        if (!dm_queue_family(d, devices[i], &queue)) continue;
        d->GetPhysicalDeviceProperties(devices[i], &p);
        d->GetPhysicalDeviceFeatures(devices[i], &f);
        unsigned score = 1u + 2u * f.shaderFloat64 + 2u * f.shaderInt64;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 32;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 16;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score += 8;
        if (score <= best_score) continue;
        best_score = score;
        best = devices[i];
        *family = queue;
        d->properties = p;
        d->features = f;
    }
    free(devices);
    return best;
}

static bool dm_init(DmDevice *d) {
    d->library = dm_open();
    if (!d->library) return false;
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dm_symbol(d->library, "vkGetInstanceProcAddr");
    d->DestroyInstance = (PFN_vkDestroyInstance)dm_symbol(d->library, "vkDestroyInstance");
    d->DestroyDevice = (PFN_vkDestroyDevice)dm_symbol(d->library, "vkDestroyDevice");
    if (!get || !d->DestroyInstance || !d->DestroyDevice) return false;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    if (!create) return false;
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DLM", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    if (create(&info, NULL, &d->instance) != VK_SUCCESS) return false;
#define DM_LOAD(name) d->name = (PFN_vk##name)get(d->instance, "vk" #name); if (!d->name) return false;
    DM_VK_INSTANCE(DM_LOAD)
#undef DM_LOAD
    PFN_vkGetDeviceProcAddr device_get = (PFN_vkGetDeviceProcAddr)get(d->instance, "vkGetDeviceProcAddr");
    if (!device_get) return false;
    uint32_t family = 0;
    VkPhysicalDevice physical = dm_choose_device(d, &family);
    if (!physical) return false;
    d->GetPhysicalDeviceMemoryProperties(physical, &d->memory);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority
    };
    VkPhysicalDeviceFeatures features = { .shaderFloat64 = d->features.shaderFloat64, .shaderInt64 = d->features.shaderInt64 };
    VkDeviceCreateInfo device = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue, .pEnabledFeatures = &features
    };
    if (d->CreateDevice(physical, &device, NULL, &d->device) != VK_SUCCESS) return false;
#define DM_LOAD(name) d->name = (PFN_vk##name)device_get(d->device, "vk" #name); if (!d->name) return false;
    DM_VK_DEVICE(DM_LOAD)
#undef DM_LOAD
    d->GetDeviceQueue(d->device, family, 0, &d->queue);

    VkDescriptorSetLayoutBinding bindings[4] = {0};
    for (uint32_t i = 0; i < 4; i++) bindings[i] = (VkDescriptorSetLayoutBinding){
        .binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    VkDescriptorSetLayoutCreateInfo descriptors = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = bindings
    };
    if (d->CreateDescriptorSetLayout(d->device, &descriptors, NULL, &d->descriptors) != VK_SUCCESS) return false;
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = 8 * sizeof(uint32_t) };
    VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
        .pSetLayouts = &d->descriptors, .pushConstantRangeCount = 1, .pPushConstantRanges = &push
    };
    if (d->CreatePipelineLayout(d->device, &layout, NULL, &d->pipeline_layout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4 };
    VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size
    };
    if (d->CreateDescriptorPool(d->device, &pool, NULL, &d->descriptor_pool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = d->descriptor_pool,
        .descriptorSetCount = 1, .pSetLayouts = &d->descriptors
    };
    if (d->AllocateDescriptorSets(d->device, &alloc, &d->descriptor_set) != VK_SUCCESS) return false;
    VkCommandPoolCreateInfo cmd_pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = family
    };
    if (d->CreateCommandPool(d->device, &cmd_pool, NULL, &d->pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo cmd = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = d->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1
    };
    if (d->AllocateCommandBuffers(d->device, &cmd, &d->command) != VK_SUCCESS) return false;
    VkFenceCreateInfo fence = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    return d->CreateFence(d->device, &fence, NULL, &d->fence) == VK_SUCCESS;
}

static bool dm_pipeline(DmDevice *d, unsigned kernel) {
    if (d->pipelines[kernel]) return true;
    if ((kernel == DM_F64_MATRIX || kernel == DM_F64_VECTOR) && !d->features.shaderFloat64) return false;
    if (kernel == DM_U32_NTT && !d->features.shaderInt64) return false;
    static const uint32_t *const codes[] = { dm_spv_matmul32, dm_spv_matmul64, dm_spv_vector32, dm_spv_vector64, dm_spv_ntt };
    static const size_t sizes[] = { sizeof(dm_spv_matmul32), sizeof(dm_spv_matmul64), sizeof(dm_spv_vector32), sizeof(dm_spv_vector64), sizeof(dm_spv_ntt) };
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizes[kernel], .pCode = codes[kernel] };
    VkShaderModule module = VK_NULL_HANDLE;
    if (d->CreateShaderModule(d->device, &shader, NULL, &module) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = d->pipeline_layout,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module, .pName = "main"
        }
    };
    VkResult status = d->CreateComputePipelines(d->device, VK_NULL_HANDLE, 1, &pipeline, NULL, &d->pipelines[kernel]);
    d->DestroyShaderModule(d->device, module, NULL);
    if (status != VK_SUCCESS && d->pipelines[kernel]) {
        d->DestroyPipeline(d->device, d->pipelines[kernel], NULL);
        d->pipelines[kernel] = VK_NULL_HANDLE;
    }
    return status == VK_SUCCESS;
}

static bool dm_bind_memory(DmDevice *d, VkBuffer buffer, VkMemoryPropertyFlags required,
                           VkMemoryPropertyFlags preferred, VkDeviceMemory *memory, VkMemoryPropertyFlags *flags) {
    VkMemoryRequirements req;
    d->GetBufferMemoryRequirements(d->device, buffer, &req);
    for (unsigned pass = 0; pass < (preferred ? 2u : 1u); pass++) {
        for (uint32_t i = 0; i < d->memory.memoryTypeCount; i++) {
            VkMemoryPropertyFlags available = d->memory.memoryTypes[i].propertyFlags;
            if (!(req.memoryTypeBits & (1u << i)) || (available & required) != required) continue;
            if (preferred && ((available & preferred) == preferred) != (pass == 0)) continue;
            VkMemoryAllocateInfo alloc = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = i
            };
            VkDeviceMemory allocated = VK_NULL_HANDLE;
            if (d->AllocateMemory(d->device, &alloc, NULL, &allocated) != VK_SUCCESS) continue;
            if (d->BindBufferMemory(d->device, buffer, allocated, 0) == VK_SUCCESS) {
                *memory = allocated;
                if (flags) *flags = available;
                return true;
            }
            d->FreeMemory(d->device, allocated, NULL);
        }
    }
    return false;
}

static bool dm_buffer(DmDevice *d, DmBuffer *b, size_t size) {
    if (size <= b->capacity) return true;
    dm_release_buffer(d, b);
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VkBuffer staging = VK_NULL_HANDLE;
    if (d->CreateBuffer(d->device, &info, NULL, &staging) != VK_SUCCESS) return false;
    b->buffer = staging;
    VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryPropertyFlags flags;
    if (!dm_bind_memory(d, b->buffer, host, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &b->memory, &flags)
        || d->MapMemory(d->device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->host) != VK_SUCCESS) goto fail;
    if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && d->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        VkBuffer local = VK_NULL_HANDLE;
        if (d->CreateBuffer(d->device, &info, NULL, &local) == VK_SUCCESS) {
            if (dm_bind_memory(d, local, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &b->local_memory, NULL))
                b->local = local;
            else
                d->DestroyBuffer(d->device, local, NULL);
        }
    }
    b->capacity = size;
    return true;
fail:
    dm_release_buffer(d, b);
    return false;
}

static void dm_barrier(DmDevice *d, VkPipelineStageFlags from, VkAccessFlags writes,
                       VkPipelineStageFlags to, VkAccessFlags reads) {
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = writes, .dstAccessMask = reads
    };
    d->CmdPipelineBarrier(d->command, from, to, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static bool dm_run(DmDevice *d, unsigned shader, const DmDispatch *steps, size_t count,
                   const DmTransfer transfers[4], unsigned output_slot, void *output, size_t output_bytes) {
    const VkPhysicalDeviceLimits *limits = &d->properties.limits;
    if (output_slot >= 4 || output_bytes > transfers[output_slot].bytes) return false;
    if (limits->maxPerStageDescriptorStorageBuffers < 4 || limits->maxDescriptorSetStorageBuffers < 4) return false;
    for (size_t i = 0; i < count; i++) {
        uint32_t x = steps[i].x, y = steps[i].y;
        if (!x || !y) return false;
        if (shader >= DM_F32_VECTOR) {
            uint32_t max_x = limits->maxComputeWorkGroupCount[0];
            y = (x - 1) / max_x + 1;
            if (x > max_x) x = max_x;
        }
        if (!x || !y || x > limits->maxComputeWorkGroupCount[0] || y > limits->maxComputeWorkGroupCount[1]) return false;
    }
    if (!dm_pipeline(d, shader)) return false;
    VkDescriptorBufferInfo buffers[4];
    VkWriteDescriptorSet writes[4];
    for (uint32_t i = 0; i < 4; i++) {
        size_t bytes = transfers[i].bytes ? transfers[i].bytes : 4;
        size_t input_bytes = transfers[i].input_bytes;
        if (input_bytes > bytes || (input_bytes && !transfers[i].input)) return false;
        if (bytes > limits->maxStorageBufferRange || !dm_buffer(d, &d->buffers[i], bytes)) return false;
        DmBuffer *buffer = &d->buffers[i];
        if (input_bytes) {
            memcpy(buffer->host, transfers[i].input, input_bytes);
            memset((char *)buffer->host + input_bytes, 0, bytes - input_bytes);
        }
        buffers[i] = (VkDescriptorBufferInfo){ .buffer = buffer->local ? buffer->local : buffer->buffer, .range = bytes };
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = d->descriptor_set,
            .dstBinding = i, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffers[i]
        };
    }
    d->UpdateDescriptorSets(d->device, 4, writes, 0, NULL);
    if (d->ResetCommandPool(d->device, d->pool, 0) != VK_SUCCESS || d->ResetFences(d->device, 1, &d->fence) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    if (d->BeginCommandBuffer(d->command, &begin) != VK_SUCCESS) return false;
    const VkPipelineStageFlags compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, transfer = VK_PIPELINE_STAGE_TRANSFER_BIT;
    bool uploaded = false;
    for (unsigned i = 0; i < 4; i++) {
        DmBuffer *b = &d->buffers[i];
        if (b->local && transfers[i].input_bytes) {
            VkBufferCopy copy = { .size = transfers[i].bytes };
            d->CmdCopyBuffer(d->command, b->buffer, b->local, 1, &copy);
            uploaded = true;
        }
    }
    if (uploaded) dm_barrier(d, transfer, VK_ACCESS_TRANSFER_WRITE_BIT, compute,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    d->CmdBindPipeline(d->command, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipelines[shader]);
    d->CmdBindDescriptorSets(d->command, VK_PIPELINE_BIND_POINT_COMPUTE, d->pipeline_layout, 0, 1, &d->descriptor_set, 0, NULL);
    for (size_t i = 0; i < count; i++) {
        if (i) dm_barrier(d, compute, VK_ACCESS_SHADER_WRITE_BIT, compute,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        uint32_t x = steps[i].x, y = steps[i].y;
        if (shader >= DM_F32_VECTOR) {
            uint32_t max_x = limits->maxComputeWorkGroupCount[0];
            y = (x - 1) / max_x + 1;
            if (x > max_x) x = max_x;
        }
        d->CmdPushConstants(d->command, d->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(steps[i].word), steps[i].word);
        d->CmdDispatch(d->command, x, y, 1);
    }
    DmBuffer *result = &d->buffers[output_slot];
    if (result->local) {
        dm_barrier(d, compute, VK_ACCESS_SHADER_WRITE_BIT, transfer, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferCopy copy = { .size = output_bytes };
        d->CmdCopyBuffer(d->command, result->local, result->buffer, 1, &copy);
        dm_barrier(d, transfer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    } else {
        dm_barrier(d, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    }
    if (d->EndCommandBuffer(d->command) != VK_SUCCESS) return false;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &d->command
    };
    if (d->QueueSubmit(d->queue, 1, &submit, d->fence) != VK_SUCCESS
        || d->WaitForFences(d->device, 1, &d->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        d->DeviceWaitIdle(d->device);
        dm_release(d);
        dm_ready = false;
        return false;
    }
    memcpy(output, result->host, output_bytes);
    return true;
}

static bool dm_vulkan(unsigned shader, const DmDispatch *dispatches, size_t count,
                      const DmTransfer transfers[4], unsigned output_slot, void *output, size_t output_bytes,
                      uint64_t work) {
    const char *selection = getenv("DLM_MATH_BACKEND");
    bool forced = selection && strcmp(selection, "vulkan") == 0;
    if (selection && strcmp(selection, "cpu") == 0) return false;
    uint64_t bytes = 0;
    for (unsigned i = 0; i < 4; i++) bytes += transfers[i].bytes;
    /* Each FFT/NTT pass streams its operands on the CPU. CSR has a lower
     * compute-to-transfer ratio; GEMM reuses tiles. Admit by kernel class,
     * including a fixed dispatch cost per pass and all transferred buffers. */
    const bool matrix = shader <= DM_F64_MATRIX;
    const bool sparse = !matrix && dispatches[0].word[0] == 0;
    uint64_t traffic_equivalent = work * (matrix ? 1u : sparse ? 16u : 8u);
    uint64_t overhead = bytes * (matrix ? 3u : 1u) + count * 32768u;
    if (!forced && (work < (1u << 20) || traffic_equivalent < overhead)) return false;
    dm_lock();
    if (!dm_attempted) {
        dm_attempted = true;
        dm_ready = dm_init(&dm_device);
        if (!dm_ready || atexit(dm_exit) != 0) {
            dm_release(&dm_device);
            dm_ready = false;
        }
    }
    bool ok = dm_ready && (forced || dm_device.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
        && dm_run(&dm_device, shader, dispatches, count, transfers, output_slot, output, output_bytes);
    dm_unlock();
    return ok;
}
#else
static bool dm_vulkan(unsigned shader, const DmDispatch *dispatches, size_t count,
                      const DmTransfer transfers[4], unsigned output_slot, void *output, size_t output_bytes,
                      uint64_t work) {
    (void)shader; (void)dispatches; (void)count; (void)transfers; (void)output_slot;
    (void)output; (void)output_bytes; (void)work;
    return false;
}
#endif
