//! Headless Vulkan device discovery. Loading the API and finding a compute queue
//! are independent of whether DLM has a compatible compute kernel.

use std::fmt;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Device {
    pub index: usize,
    pub compute_queue_family: Option<usize>,
    pub dedicated_compute: bool,
    pub shader_float64: bool,
    pub shader_int64: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ProbeError {
    UnsupportedPlatform,
    LoaderUnavailable,
    MissingFunction(&'static str),
    Driver { function: &'static str, result: i32 },
    InvalidResponse(&'static str),
    Allocation,
}

impl fmt::Display for ProbeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnsupportedPlatform => f.write_str("platform is unsupported"),
            Self::LoaderUnavailable => f.write_str("system Vulkan loader unavailable"),
            Self::MissingFunction(name) => write!(f, "loader did not resolve {name}"),
            Self::Driver { function, result } => write!(f, "{function} returned VkResult {result}"),
            Self::InvalidResponse(function) => write!(f, "{function} returned an invalid response"),
            Self::Allocation => f.write_str("could not allocate the device discovery list"),
        }
    }
}

pub fn probe() -> Result<Vec<Device>, ProbeError> {
    #[cfg(any(windows, target_os = "linux"))]
    {
        platform::probe()
    }
    #[cfg(not(any(windows, target_os = "linux")))]
    {
        Err(ProbeError::UnsupportedPlatform)
    }
}

#[cfg(any(windows, target_os = "linux"))]
mod platform {
    use super::{Device, ProbeError};
    use std::ffi::{c_char, c_void, CStr};
    use std::ptr::{null, null_mut};

    type Handle = *mut c_void;
    type GetProc = unsafe extern "system" fn(Handle, *const c_char) -> Handle;
    type CreateInstance =
        unsafe extern "system" fn(*const InstanceInfo, *const c_void, *mut Handle) -> i32;
    type DestroyInstance = unsafe extern "system" fn(Handle, *const c_void);
    type EnumerateDevices = unsafe extern "system" fn(Handle, *mut u32, *mut Handle) -> i32;
    type GetQueues = unsafe extern "system" fn(Handle, *mut u32, *mut QueueProperties);
    type GetFeatures = unsafe extern "system" fn(Handle, *mut Features);

    const SUCCESS: i32 = 0;
    const INCOMPLETE: i32 = 5;
    const GRAPHICS: u32 = 0x1;
    const COMPUTE: u32 = 0x2;

    #[repr(C)]
    struct InstanceInfo {
        structure_type: u32,
        next: *const c_void,
        flags: u32,
        application: *const c_void,
        layer_count: u32,
        layers: *const *const c_char,
        extension_count: u32,
        extensions: *const *const c_char,
    }

    #[repr(C)]
    #[derive(Copy, Clone, Default)]
    struct QueueProperties {
        flags: u32,
        count: u32,
        timestamp_valid_bits: u32,
        min_transfer_granularity: [u32; 3],
    }

    // Vulkan 1.0 VkPhysicalDeviceFeatures has 55 consecutive VkBool32 fields.
    #[repr(C)]
    struct Features {
        before_shader_float64: [u32; 39],
        shader_float64: u32,
        shader_int64: u32,
        remaining: [u32; 14],
    }

    struct Instance {
        handle: Handle,
        destroy: DestroyInstance,
    }

    impl Drop for Instance {
        fn drop(&mut self) {
            // SAFETY: this is the live instance and allocator supplied at creation.
            unsafe { (self.destroy)(self.handle, null()) }
        }
    }

    pub(super) fn probe() -> Result<Vec<Device>, ProbeError> {
        let loader = Loader::open()?;
        macro_rules! function {
            ($address:expr, $name:literal, $signature:ty) => {{
                let address = $address;
                if address.is_null() {
                    return Err(ProbeError::MissingFunction($name));
                }
                // SAFETY: the name and signature identify the same Vulkan 1.0 entry point.
                unsafe { std::mem::transmute::<Handle, $signature>(address) }
            }};
        }

        let get_proc = function!(
            loader.symbol(c"vkGetInstanceProcAddr"),
            "vkGetInstanceProcAddr",
            GetProc
        );
        let create = function!(
            proc_at(get_proc, null_mut(), c"vkCreateInstance"),
            "vkCreateInstance",
            CreateInstance
        );
        let destroy = function!(
            loader.symbol(c"vkDestroyInstance"),
            "vkDestroyInstance",
            DestroyInstance
        );
        let info = InstanceInfo {
            structure_type: 1, // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
            next: null(),
            flags: 0,
            application: null(),
            layer_count: 0,
            layers: null(),
            extension_count: 0,
            extensions: null(),
        };
        let mut handle = null_mut();
        // SAFETY: Vulkan receives a complete headless VkInstanceCreateInfo and a writable handle.
        check(
            unsafe { create(&info, null(), &mut handle) },
            "vkCreateInstance",
        )?;
        if handle.is_null() {
            return Err(ProbeError::InvalidResponse("vkCreateInstance"));
        }
        let instance = Instance { handle, destroy };
        let enumerate = function!(
            proc_at(get_proc, instance.handle, c"vkEnumeratePhysicalDevices"),
            "vkEnumeratePhysicalDevices",
            EnumerateDevices
        );
        let get_queues = function!(
            proc_at(
                get_proc,
                instance.handle,
                c"vkGetPhysicalDeviceQueueFamilyProperties"
            ),
            "vkGetPhysicalDeviceQueueFamilyProperties",
            GetQueues
        );
        let get_features = function!(
            proc_at(get_proc, instance.handle, c"vkGetPhysicalDeviceFeatures"),
            "vkGetPhysicalDeviceFeatures",
            GetFeatures
        );

        enumerate_devices(&instance, enumerate)?
            .into_iter()
            .enumerate()
            .map(|(index, device)| device_report(index, device, get_queues, get_features))
            .collect()
    }

    fn proc_at(get_proc: GetProc, instance: Handle, name: &CStr) -> Handle {
        // SAFETY: the name is nul-terminated and the handle is null or live.
        unsafe { get_proc(instance, name.as_ptr()) }
    }

    fn check(result: i32, function: &'static str) -> Result<(), ProbeError> {
        if result == SUCCESS {
            Ok(())
        } else {
            Err(ProbeError::Driver { function, result })
        }
    }

    fn allocate<T: Copy>(length: u32, default: T) -> Result<Vec<T>, ProbeError> {
        let mut items = Vec::new();
        items
            .try_reserve_exact(length as usize)
            .map_err(|_| ProbeError::Allocation)?;
        items.resize(length as usize, default);
        Ok(items)
    }

    fn enumerate_devices(
        instance: &Instance,
        enumerate: EnumerateDevices,
    ) -> Result<Vec<Handle>, ProbeError> {
        const CALL: &str = "vkEnumeratePhysicalDevices";
        for _ in 0..3 {
            let mut count = 0;
            // SAFETY: null requests a count, and the next call passes storage for that count.
            check(
                unsafe { enumerate(instance.handle, &mut count, null_mut()) },
                CALL,
            )?;
            if count == 0 {
                return Ok(Vec::new());
            }
            let mut devices = allocate(count, null_mut())?;
            let result = unsafe { enumerate(instance.handle, &mut count, devices.as_mut_ptr()) };
            if result == INCOMPLETE {
                continue;
            }
            check(result, CALL)?;
            if count as usize > devices.len()
                || devices[..count as usize].iter().any(|d| d.is_null())
            {
                return Err(ProbeError::InvalidResponse(CALL));
            }
            devices.truncate(count as usize);
            return Ok(devices);
        }
        Err(ProbeError::Driver {
            function: CALL,
            result: INCOMPLETE,
        })
    }

    fn device_report(
        index: usize,
        device: Handle,
        get_queues: GetQueues,
        get_features: GetFeatures,
    ) -> Result<Device, ProbeError> {
        let mut count = 0;
        // SAFETY: device was enumerated from the live instance. The first call
        // obtains the queue family count and the second passes that much storage.
        unsafe { get_queues(device, &mut count, null_mut()) }
        let mut queues = allocate(count, QueueProperties::default())?;
        if count != 0 {
            unsafe { get_queues(device, &mut count, queues.as_mut_ptr()) }
        }
        if count as usize > queues.len() {
            return Err(ProbeError::InvalidResponse(
                "vkGetPhysicalDeviceQueueFamilyProperties",
            ));
        }
        queues.truncate(count as usize);
        let queue = choose_compute_queue(&queues);
        let mut features = Features {
            before_shader_float64: [0; 39],
            shader_float64: 0,
            shader_int64: 0,
            remaining: [0; 14],
        };
        // SAFETY: features has the Vulkan 1.0 field layout.
        unsafe { get_features(device, &mut features) }
        Ok(Device {
            index,
            compute_queue_family: queue,
            dedicated_compute: queue.is_some_and(|q| queues[q].flags & GRAPHICS == 0),
            shader_float64: features.shader_float64 != 0,
            shader_int64: features.shader_int64 != 0,
        })
    }

    fn choose_compute_queue(queues: &[QueueProperties]) -> Option<usize> {
        queues
            .iter()
            .enumerate()
            .filter(|(_, queue)| queue.count != 0 && queue.flags & COMPUTE != 0)
            .min_by_key(|(_, queue)| queue.flags & GRAPHICS != 0)
            .map(|(index, _)| index)
    }

    struct Loader(Handle);

    impl Loader {
        fn open() -> Result<Self, ProbeError> {
            #[cfg(target_os = "linux")]
            // SAFETY: the loader soname is static and nul-terminated, with RTLD_NOW.
            let library = unsafe { dlopen(c"libvulkan.so.1".as_ptr(), 2) };
            #[cfg(windows)]
            // SAFETY: LoadLibraryExW reads a nul-terminated UTF-16 name from the system directory.
            let library = unsafe {
                let name: Vec<_> = "vulkan-1.dll".encode_utf16().chain([0]).collect();
                LoadLibraryExW(name.as_ptr(), null_mut(), 0x800)
            };
            if library.is_null() {
                Err(ProbeError::LoaderUnavailable)
            } else {
                Ok(Self(library))
            }
        }

        fn symbol(&self, name: &CStr) -> Handle {
            // SAFETY: the handle came from the platform loader and the name has a terminator.
            unsafe {
                #[cfg(target_os = "linux")]
                {
                    dlsym(self.0, name.as_ptr())
                }
                #[cfg(windows)]
                {
                    GetProcAddress(self.0, name.as_ptr())
                }
            }
        }
    }

    impl Drop for Loader {
        fn drop(&mut self) {
            // SAFETY: the instance and its commands have been released first.
            unsafe {
                #[cfg(target_os = "linux")]
                dlclose(self.0);
                #[cfg(windows)]
                FreeLibrary(self.0);
            }
        }
    }

    #[cfg(target_os = "linux")]
    #[link(name = "dl")]
    unsafe extern "C" {
        fn dlopen(name: *const c_char, flags: i32) -> Handle;
        fn dlsym(library: Handle, name: *const c_char) -> Handle;
        fn dlclose(library: Handle) -> i32;
    }
    #[cfg(windows)]
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn LoadLibraryExW(name: *const u16, file: Handle, flags: u32) -> Handle;
        fn GetProcAddress(library: Handle, name: *const c_char) -> Handle;
        fn FreeLibrary(library: Handle) -> i32;
    }

    #[cfg(test)]
    #[test]
    fn compute_queue_prefers_dedicated_nonempty_family() {
        fn queue(flags: u32, count: u32) -> QueueProperties {
            QueueProperties {
                flags,
                count,
                ..QueueProperties::default()
            }
        }
        let mixed = queue(GRAPHICS | COMPUTE, 1);
        assert_eq!(
            choose_compute_queue(&[queue(GRAPHICS, 1), mixed, queue(COMPUTE, 0)]),
            Some(1)
        );
        assert_eq!(choose_compute_queue(&[mixed, queue(COMPUTE, 1)]), Some(1));
        assert_eq!(
            choose_compute_queue(&[queue(GRAPHICS, 2), queue(COMPUTE, 0)]),
            None
        );
    }
}
