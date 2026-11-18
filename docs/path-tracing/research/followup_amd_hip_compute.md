# AMD GPU compute route on Windows (HIP) for EV: RX 9070 XT (gfx1201) + RTX 3070 (sm_86), sharing buffers and a fence with D3D12

Research notes as of 2026-11-18. They build on `optix_cuda_interop.md` (CUDA <-> D3D12 interop, OIDN overview) and `neural_radiance_cache.md` (NRC algorithm, tiny-cuda-nn, HLSL LinAlg). Facts already verified there are not repeated. "Source code" citations mean I read the file myself through the GitHub API on 2026-11-18. Anything not backed by a fetched source is in Inferences or Gaps.

---

## 1. HIP SDK for Windows: versions, install, gfx1201 support, Visual Studio / CMake, compiling, DLLs, drivers

### Takeaway
As of November 2026 there are **two Windows HIP distributions**:
- the **legacy "HIP SDK for Windows" installer, 7.2.x** (GUI/CLI `.exe`, bundles a driver, VS plugin and ROCgdb);
- the new **ROCm Core SDK ("TheRock") 10.1.0** (2026-10-05). It is a tarball or pip-wheel install for "Linux and Windows" with HIP runtime 7.16.0, LLVM 24, rocWMMA 2.2.1 and Adrenalin 26.10.41.05.

The RX 9070 XT (gfx1201) is fully supported by both: Runtime, HIP SDK, and on the legacy SDK also the debugger. Build with `hipcc ... --offload-arch=gfx1201`. The HIP runtime (`amdhip64_7.dll`) ships **inside the AMD driver** and must not be redistributed. Only hipRTC or HIP libraries you use need shipping. Visual Studio integration is the HIP-VS extension (documented for VS 2017/2019/2022 only). CMake's HIP language on Windows is officially "unsupported" in the legacy SDK and fiddly in practice.

### Cited Findings
**Versions and which stream is current**
- The legacy HIP SDK for Windows docs are labelled: "This is legacy documentation. For the latest ROCm documentation, visit rocm.docs.amd.com." The sidebar says "HIP SDK for Windows 7.2.0 documentation". — [HIP SDK for Windows landing](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/)
- HIP SDK for Windows 7.2.0 release notes, dated 2026-07-31:
  - "Support for ROCm 7.2.0";
  - new Ryzen AI 300/400/Max 400 APU support;
  - ROCgdb performance improvements on Windows;
  - known issue: "Applications using the HIP 6 runtime (amdhip64_6.dll) show ~20% lower performance on Strix Halo" with Adrenalin 26.10-branch drivers, while "Applications on the HIP 7 runtime (amdhip64_7.dll) are unaffected."
  - — [HIP SDK 7.2.0 release notes](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/about/releasenotes.html)
- The HIP-VS docs banner says "ROCm 7.13.0 is a technology preview" and "For production use, continue to use ROCm 7.2.4 documentation." So 7.2.4 is the newest legacy point release that I saw referenced. — [HIP-VS installing](https://rocm.docs.amd.com/projects/hip-vs/en/latest/installing.html)
- ROCm Core SDK 10.1.0 release notes:
  - dated 2026-10-05, "Applies to Linux and Windows";
  - Windows OS row: "Windows | 11 25H2";
  - Radeon Windows driver: "AMD Software: Adrenalin Edition" 26.10.41.05;
  - HIP 7.16.0 (up from 7.15.0), LLVM 24.0.0, rocWMMA 2.2.1, all listed for "Linux/Windows";
  - gfx1201 covers the RX 9070 series; rocSPARSE has "wave32-aware ... tuning on RDNA4 (gfx1201)";
  - ROCgdb and the ROCm profilers (ROCprofiler-SDK, Compute/Systems Profiler) are listed as **Linux only**.
  - — [ROCm Core SDK 10.1.0 release notes](https://rocm.docs.amd.com/en/latest/about/release-notes.html)
- "The transition from the legacy ROCm release stream began with ROCm Core SDK 7.14.0, the first production release." "ROCm 10.1.0 is the latest production release." The page does not explain the jump from 7.x to 10.x numbering. — [Transition guide from legacy ROCm (TheRock)](https://rocm.docs.amd.com/en/latest/_sources/about/transition-guide-TheRock.md)
- A search snippet of the 7.1.1 Windows release notes says the HIP SDK for ROCm 7.x on Windows "is not backwards compatible with previous versions" and ships updated `amdhip64` and `amd_comgr`. I saw this only as a snippet, not on the fetched page. — [HIP SDK 7.1.1 release notes](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/about/releasenotes.html)

**gfx1201 support status**
- The HIP SDK for Windows 7.2.0 system requirements list the Radeon RX 9070 XT (RDNA4, LLVM target gfx1201) as ✅ for Runtime, HIP SDK and AMD ROCm Debugger. ✅ means "Official software distributions of the current HIP SDK release fully support this hardware."
  - RX 9070, 9070 GRE, 9060 XT and 9060 are also fully supported.
  - RX 7000 (RDNA3) is ✅ for Runtime and SDK, but ❌ for the debugger.
  - RDNA2 cards (RX 6800/6600) are ❌.
  - OS: "Windows 11", validated update 22H2.
  - "ROCm requires CPUs that support PCIe™ atomics."
  - — [HIP SDK 7.2 system requirements](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html)

**Installing: legacy HIP SDK 7.2 (GUI or CLI installer)**
- The installer is an `.exe` that needs admin rights. Components (Full/Partial/None):
  - HIP SDK Core
  - HIP Libraries
  - HIP Runtime Compiler
  - HIP Ray Tracing
  - Visual Studio Plugin (VS 2017, 2019, 2022)
  - AMD ROCm Debugger (ROCgdb)
- It bundles an "AMD Radeon Software PRO 25.30" driver, with Full, Minimal or Driver Only options. A "Factory Reset" option removes earlier HIP SDK and driver versions.
- CLI options: `-install`, `-install -boot`, `-install -log <path>`, `-uninstall`. The CLI cannot install a subset.
- Add the HIP `bin` directory to PATH; the docs' example is `C:\Program Files\AMD\ROCm\7.1\bin`. Verify with `hipInfo` and `hipconfig`.
- — [Install HIP SDK 7.2](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/install/install.html)

**Installing: ROCm Core SDK 10.1.0 on Windows**
1. Remove any existing HIP SDK for Windows through Control Panel. Turn off WDAG and Smart App Control, which "can interfere with ROCm functionality".
2. Install Python 3.11–3.14 and the Adrenalin driver.
3. Download and extract a tarball into `C:\TheRock\build`. Per-family tarballs exist, including `gfx120X-all` (RDNA4) and `multiarch`. Example: `therock-dist-windows-multiarch-10.1.0.tar.gz` from `https://stable.repo.amd.com/rocm/core/tarball/`.
4. Optional pip route: `python -m pip install --index-url https://stable.repo.amd.com/rocm/whl-next/ "rocm[libraries,device-gfx1201]==10.1.0"`. `rocm[devel]` adds compilers and headers.
5. Set environment variables with `setx`: `HIP_DEVICE_LIB_PATH=C:\TheRock\build\lib\llvm\amdgcn\bitcode`, `HIP_PATH=C:\TheRock\build`, `HIP_PLATFORM=amd`, `LLVM_PATH=C:\TheRock\build\lib\llvm`, and add `...\build\bin` and `...\lib\llvm\bin` to PATH.
6. Verify with `hipinfo`.
- Caution: "Do not replace or copy the ROCm compiler and runtime DLLs to System32."
- — [Install ROCm Core SDK 10.1.0](https://rocm.docs.amd.com/en/latest/install/rocm.html)

**Visual Studio, MSBuild and CMake**
- HIP-VS is installed as a `.vsix` per VS version (folder `HIPExtension.20XX`, where XX is 17, 19 or 22), via the wizard or `VSIXInstaller.exe /a /q`. VS 2026 is not mentioned. — [HIP-VS installing](https://rocm.docs.amd.com/projects/hip-vs/en/latest/installing.html)
- HIP SDK 7.2 component table:
  - Windows has "hipcc / clang++", the HIP runtime ("closed source"), math and primitives libraries, HIP Ray Tracing, HIPIFY, hipInfo, a Visual Studio plugin, the ROCm Debugger and the Radeon GPU Profiler.
  - The **CMake HIP language is listed as unsupported** on Windows.
  - MIOpen, MIGraphX and PyTorch are not available.
  - — [HIP SDK 7.2 component support](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/conceptual/component-support.html)
- AMD's own porting-skill notes (amd/skills repo) on CMake's HIP language on Windows:
  - It "only works when every C/C++ compiler is ROCm's GNU-style `clang++`". With cl.exe as CXX, `enable_language(HIP)` fails with "mixes Clang and MSVC".
  - CMake's default `CMAKE_HIP_ARCHITECTURES` detection uses `rocm_agent_enumerator`, "which ROCm does not ship on Windows", and "silently falls back to gfx906". Use `amdgpu-arch` instead.
  - CMake before 3.29 links a Windows HIP DLL without `-shared` (3.28.4 fails, 3.29.2 works).
  - A Visual Studio generator "has no HIP support"; use Ninja.
  - Clang predefines `_WIN32` but not `WIN32`, so `#ifdef WIN32` export macros export nothing.
  - — [amd/skills moat-cuda-to-rocm fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)
- OIDN's own HIP build also requires CMake 3.21+ with Ninja or Make: "The Visual Studio generator is *not* supported." — [OIDN README](https://raw.githubusercontent.com/RenderKit/oidn/master/README.md)
- rocm-examples ships Visual Studio 2017/2019/2022 solution files. "The Visual Studio ROCm extension needs to be installed to build with the solution files." — [ROCm/rocm-examples README](https://raw.githubusercontent.com/ROCm/rocm-examples/amd-staging/README.md)

**Compiling**
- AMD's Windows debugger page uses `hipcc example.cpp -o example.exe --offload-arch=gfx1201 -gdwarf -Wl,-debug:dwarf -O0` (gfx1200 for the RX 9060). — [AMD ROCm Debugger for Windows](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/how-to/debugger-windows.html)
- Blender Cycles shows the "offline code object" pattern on Windows:
  - It runs `cmake -E env HIP_PATH=... hipcc --offload-arch=${arch} --genco kernel.cpp -ffast-math -std=c++17 -o kernel_${arch}.fatbin` once per arch, then zstd-compresses the result.
  - The host loads it with `hipModuleLoadData`. The host code is not compiled by hipcc; HIP is loaded dynamically through Blender's `extern/hipew`.
  - On Windows it uses `-O1` for gfx90x and HIP 6.1.40252 because of compiler bugs.
  - — [Blender intern/cycles/kernel/device/hip/CMakeLists.txt](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/CMakeLists.txt); [Blender device/hip/device_impl.cpp (hipModuleLoadData)](https://github.com/blender/blender/blob/main/intern/cycles/device/hip/device_impl.cpp)
- Blender's default HIP arch list includes `gfx1200 gfx1201`. Its CUDA list is `sm_50 sm_52 sm_60 sm_70 sm_75 sm_86 sm_120 compute_75`. — [Blender root CMakeLists.txt](https://github.com/blender/blender/blob/main/CMakeLists.txt)
- hipcc env vars: `HIP_PLATFORM` is `amd` or `nvidia`; "If HIP_PLATFORM isn't set, then HIPCC attempts to auto-detect the platform based on whether the nvcc tool is found." `HIP_PATH` is "The path of the HIP SDK on Microsoft Windows for AMD platforms". — [HIPCC environment variables](https://rocm.docs.amd.com/projects/HIPCC/en/latest/env.html)

**Runtime DLLs and driver**
- "AMD GPU drivers are distributed with the HIP runtime included." "Applications do not distribute the HIP runtime."
  - Apps that use dynamically linked HIP libraries should ship those libraries.
  - "AMD does not support static linking to any HIP SDK component."
  - Document the minimum HIP runtime and driver. ABI breaks happen only when the HIP runtime major version increments.
  - — [HIP SDK deployment guidelines](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/conceptual/deployment-guidelines.html)
- Orochi's HIP loader (hipew) tries these Windows DLL names in order: `amdhip64_8.dll`, `amdhip64_7.dll`, `amdhip64_6.dll`, `amdhip64.dll` ("hip '5.x' DLL"). For hipRTC it tries versioned names `hiprtc0801.dll`, `hiprtc0800.dll`, `hiprtc0717.dll`, `hiprtc0716.dll`, ... `hiprtc0702.dll`, ... `hiprtc0503.dll`. — [Orochi contrib/hipew/src/hipew.cpp](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/contrib/hipew/src/hipew.cpp) (source read, lines 625–663)
- Orochi commit history: "Fix loading hiprtc0716 on Windows" and "Correct Windows HIPRTC 7.14 and 7.15 names" (2026-09-24). — [Orochi commits](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/commits/main)
- "on Windows the hipRTC DLL carries the version, `hiprtc0702.dll`, so scan `bin`". — [amd/skills fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)

### Inferences
- **Which SDK to install today:** for a student on an RX 9070 XT who wants Visual Studio integration and ROCgdb, the legacy **HIP SDK 7.2.x installer** is the smoother choice. It has a GUI, the VS plugin and the debugger, and gfx1201 is fully supported.
  - ROCm Core SDK 10.1 is the forward path (newer HIP 7.16 and LLVM 24).
  - Its release notes list ROCgdb as Linux-only, and its install steps never mention the VS plugin.
  - They cannot be installed side by side: the 10.1 guide says to remove the HIP SDK first.
  - Pin one in the build docs (`HIP_PATH`).
- **Build EV-Hip.dll without hipcc driving the whole DLL** (the Blender pattern):
  - Compile only the kernel files with `hipcc --genco --offload-arch=gfx1201` into a `.hsaco`/fatbin as a custom build step.
  - Build the host part of EV-Hip.dll with normal MSVC, using HIP headers or a dynamic loader (hipew/Orochi).
  - Load the kernels with `hipModuleLoadData` / `hipModuleGetFunction` / `hipModuleLaunchKernel`.
  - This sidesteps the "CMake HIP language needs clang++ for everything" problem.
- Alternatively, compile all of EV-Hip.dll with hipcc/clang++ (single-source `<<<>>>` launches) via the HIP-VS toolset or Ninja + clang++. That is simpler code, but the whole DLL then uses the clang toolchain. Keep the D3D12 engine in MSVC and expose only a C ABI from the DLL.
- **Runtime dependency:** the end user (here: the student's own PCs) needs only the Adrenalin driver, which provides `amdhip64_7.dll`. If you use hipRTC (runtime compilation, or Orochi's `orortc`), you also need the versioned `hiprtc*.dll` from the SDK next to the exe.
- **Driver versions:** the HIP SDK 7.2 installer's bundled driver is "PRO 25.30". ROCm 10.1 names Adrenalin 26.10.41.05. OIDN needs ≥ 25.30.02.01 (section 6). A current Adrenalin driver satisfies all three, but ROCgdb wants the SDK-bundled driver (section 7).

### Gaps
- I found no statement that HIP-VS supports Visual Studio 2026 (18.x). Only 2017/2019/2022 are documented.
- There is no 7.2.x point-release list (7.2.1–7.2.4) for the Windows installer with dates. The 7.2.4 reference comes only from a docs banner.
- The default install path of HIP SDK 7.2 (`C:\Program Files\AMD\ROCm\7.2\`?) is not stated. The docs' example still says 7.1.
- I found no explanation of why ROCm Core SDK went from 7.14–7.16 to "10.1.0" while the HIP runtime inside is 7.16.0.
- Whether the Adrenalin consumer driver ships `hiprtc*.dll` (needed for runtime compilation without the SDK) was not verified.

---

## 2. HIP <-> D3D12 interop on Windows: does it work, and how (memory, fences, limitations)

### Takeaway
**Yes. It works on Windows today, with a specific subset.** Several independent, maintained code bases import D3D12 shared resources and fences into HIP on Windows:
- AMD's own **Orochi `Test/SimpleD3D12`** (the cuda-samples simpleD3D12 port, runs on AMD and NVIDIA, touched Oct 2026);
- Intel **OIDN 2.5's HIP device**;
- AMD **rocDecode's Windows path**.

The recipe mirrors CUDA:
1. Create a committed buffer with `D3D12_HEAP_FLAG_SHARED`, call `CreateSharedHandle`, and import it as `hipExternalMemoryHandleTypeD3D12Resource` (=5) with `hipExternalMemoryDedicated` and `size = GetResourceAllocationInfo().SizeInBytes`.
2. Map it with `hipExternalMemoryGetMappedBuffer`.
3. Create the fence with `D3D12_FENCE_FLAG_SHARED`, share it, import it as `hipExternalSemaphoreHandleTypeD3D12Fence` (=4), and use `hipWait/SignalExternalSemaphoresAsync` with `params.fence.value`.

The HIP front-end source accepts only semaphore types 1–4 (OpaqueFd, OpaqueWin32, OpaqueWin32Kmt, D3D12Fence). It **rejects D3D11 fences, keyed mutexes and timeline-semaphore handle types**. External semaphores are not supported on Linux at all. Use buffers, not textures: AMD's own rocDecode copies tiled D3D12 textures into a linear shared buffer before importing.

### Cited Findings
**HIP runtime source: what the front-end accepts** — [ROCm/rocm-systems projects/clr/hipamd/src/hip_memory.cpp](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/clr/hipamd/src/hip_memory.cpp) (source read, lines 192–378)
- `hipImportExternalMemory`:
  - It returns `hipErrorInvalidValue` unless `flags` is 0 or `hipExternalMemoryDedicated` and `size != 0`.
  - The type must lie in `[hipExternalMemoryHandleTypeOpaqueFd, hipExternalMemoryHandleTypeD3D11ResourceKmt]`, so D3D12Heap (4) and D3D12Resource (5) are accepted.
  - On `_WIN32` it constructs an `amd::ExternalBuffer` from `handle.win32.handle` / `handle.win32.name`.
  - If creation fails it returns `hipErrorOutOfMemory`.
- `hipExternalMemoryGetMappedBuffer`:
  - `flags` must be 0, and the offset and size must lie within the imported size.
  - It creates a sub-buffer "view" and returns its device virtual address.
- `hipImportExternalSemaphore`:
  - `if ((semHandleDesc->type < hipExternalSemaphoreHandleTypeOpaqueFd) || (semHandleDesc->type > hipExternalSemaphoreHandleTypeD3D12Fence)) HIP_RETURN(hipErrorInvalidValue);`
  - On Windows a null `handle.win32.handle` is invalid, so import by name is not usable for semaphores.
  - A backend failure returns `hipErrorNotSupported`.
- `hipSignalExternalSemaphoresAsync` / `hipWaitExternalSemaphoresAsync`:
  - They enqueue an `ExternalSemaphoreCmd` with `paramsArray[i].params.fence.value`.
  - `flags` must be 0 ("hipExternalSemaphoreHandleTypeNvSciSync is not supported on AMD device").
- Enum values (from HIP headers as mirrored by bindings):
  - memory: OpaqueFd=1, OpaqueWin32=2, OpaqueWin32Kmt=3, D3D12Heap=4, D3D12Resource=5, D3D11Resource=6, D3D11ResourceKmt=7, NvSciBuf=8;
  - semaphores: OpaqueFd=1, OpaqueWin32=2, OpaqueWin32Kmt=3, D3D12Fence=4, D3D11Fence=5, NvSciSync=6, KeyedMutex=7, KeyedMutexKmt=8, TimelineSemaphoreFd=9, TimelineSemaphoreWin32=10.
  - — [tinygrad autogen hip.py (enum dump)](https://github.com/tinygrad/tinygrad/blob/master/tinygrad/runtime/autogen/hip.py); [JuliaGPU AMDGPU.jl libhip.jl](https://github.com/JuliaGPU/AMDGPU.jl/blob/master/src/hip/libhip.jl)
- In the Windows (PAL) backend, `Device::importExtSemaphore` sets `palOpenInfo.flags.sharedViaNtHandle` for `OpaqueWin32` or `D3D12Fence`, plus `isReference = true` and `crossProcess = false`, and opens it as a PAL external queue semaphore. — [ROCm/rocm-systems projects/clr/rocclr/device/pal/paldevice.cpp](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/clr/rocclr/device/pal/paldevice.cpp) (source read, lines 3025–3040)
- HIP API reference (7.14.1): `hipImportExternalSemaphore`, `hipSignalExternalSemaphoresAsync`, `hipWaitExternalSemaphoresAsync` and `hipDestroyExternalSemaphore` each say "This API is currently not supported on Linux." The memory functions have no platform note. — [HIP 7.14 External Resource Interoperability](https://rocm.docs.amd.com/projects/HIP/en/docs-7.14.1/doxygen/html/group___external.html)
- The HIP how-to page (HIP 7.16 docs) shows only a HIP–Vulkan example: OpaqueWin32Kmt memory and OpaqueWin32 semaphores on Windows. D3D12 is not mentioned. — [HIP external resource interoperability how-to](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/external_interop.html)
- HIPIFY maps `cudaExternalMemoryHandleTypeD3D12Resource` to `hipExternalMemoryHandleTypeD3D12Resource` (in HIP since 4.3.0) and `cudaExternalSemaphoreHandleTypeD3D12Fence` to `hip...D3D12Fence` (since 4.4.0). — [HIPIFY CUDA Runtime API supported by HIP](https://github.com/ROCm/HIPIFY/blob/amd-staging/docs/reference/tables/CUDA_Runtime_API_functions_supported_by_HIP.md)

**Working example 1: Orochi Test/SimpleD3D12 (AMD GPUOpen, MIT; runs on AMD and NVIDIA)** — [Orochi Test/SimpleD3D12/simpleD3D12.cpp](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/Test/SimpleD3D12/simpleD3D12.cpp) (source read); [README](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/Test/SimpleD3D12/README.md)
- README: based on NVIDIA cuda-samples simpleD3D12. "DX12 and CUDA/hip synchronizes using DirectX12 Fences." "A DirectX12 Capable NVIDIA/AMD GPU is required."
- Memory:
  - `CreateCommittedResource(DEFAULT heap, D3D12_HEAP_FLAG_SHARED, Buffer(vertexBufferSize), ...)`;
  - `CreateSharedHandle(res, &windowsSecurityAttributes, GENERIC_ALL, NULL, &sharedHandle)`;
  - `size = GetResourceAllocationInfo(m_nodeMask, 1, &bufferDesc).SizeInBytes`;
  - `type = oroExternalMemoryHandleTypeD3D12Resource`, `flags = ORO_EXTERNAL_MEMORY_DEDICATED`;
  - `oroImportExternalMemory`, then **`CloseHandle(sharedHandle)` right after the import**;
  - `oroExternalMemoryGetMappedBuffer(offset 0, size = vertexBufferSize)`.
- Fence:
  - `CreateFence(..., D3D12_FENCE_FLAG_SHARED)`, then `CreateSharedHandle`;
  - `type = oroExternalSemaphoreHandleTypeD3D12Fence`, `flags = 0`, then `oroImportExternalSemaphore`;
  - per frame, `oroWaitExternalSemaphoresAsync` / `oroSignalExternalSemaphoresAsync` with `params.fence.value`, on a stream or `oroStream(0)`.
- The D3D12 adapter LUID is kept from `DXGI_ADAPTER_DESC1::AdapterLuid`.
- Latest commits include "Fix clang-cl errors and warnings in simpleD3D12" (2026-10-16, merged 2026-10-29). — [Orochi commits](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/commits/main)

**Working example 2: OIDN HIP device (Intel, Apache-2.0)**
- `HIPExternalBuffer` (Win32 constructor):
  - `D3D12Heap` maps to `hipExternalMemoryHandleTypeD3D12Heap`.
  - `D3D12Resource` maps to `hipExternalMemoryHandleTypeD3D12Resource` with `handleDesc.flags = hipExternalMemoryDedicated; // mandatory`. D3D11 resources are also forced to dedicated.
  - It maps the whole range (offset 0, size = byteSize). On mapping failure it calls `hipDestroyExternalMemory`.
  - The destructor calls `hipFree(ptr)` (HIP ≥ 6) and then `hipDestroyExternalMemory`.
  - — [oidn devices/hip/hip_external_buffer.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_external_buffer.cpp)
- `HIPExternalSemaphore` maps the OIDN flags OpaqueWin32/Win32KMT, D3D11Fence, D3D12Fence, KeyedMutex/KMT and TimelineSemaphoreWin32 to the HIP types. The FD constructor throws: "Semaphores not supported by HIP on Linux yet". — [oidn devices/hip/hip_external_semaphore.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_external_semaphore.cpp)
- `HIPDevice::init` on `_WIN32`:
  - `externalMemoryTypes` = OpaqueWin32 | OpaqueWin32KMT | D3D11Texture | D3D11TextureKMT | D3D11Resource | D3D11ResourceKMT | D3D12Heap | D3D12Resource, then `|= Dedicated`;
  - `externalSemaphoreTypes` = OpaqueWin32 | OpaqueWin32KMT | D3D11Fence | D3D12Fence | KeyedMutex | KeyedMutexKMT | TimelineSemaphoreWin32;
  - Linux: memory is OpaqueFD, semaphores are None.
  - The LUID comes from `prop.luid` and `prop.luidDeviceNodeMask` (only under `#if defined(_WIN32) && HIP_VERSION_MAJOR >= 6`).
  - — [oidn devices/hip/hip_device.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_device.cpp)
- `HIPEngine::submitSignalSemaphores` and `submitWaitSemaphores` call `hipSignalExternalSemaphoresAsync` / `hipWaitExternalSemaphoresAsync` on the engine stream, filling `params.fence.value` (or `params.keyedMutex.key/timeoutMs` for keyed mutexes). — [oidn devices/hip/hip_engine.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_engine.cpp)
- OIDN v2.3.1 "Added LUID support for HIP devices." Importing DX12 and Vulkan buffers is "now functional when using recent AMD GPU drivers on Windows." v2.5.0 adds semaphore import, "supported only by CUDA (Windows and Linux) and HIP (Windows only) devices". — [OIDN releases](https://github.com/RenderKit/oidn/releases)

**Working example 3: AMD rocDecode on Windows (first-party AMD)** — [rocm-systems projects/rocdecode/src/rocdecode/roc_decoder.cpp](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/rocdecode/src/rocdecode/roc_decoder.cpp); [vaapi/d3d12_interop.cpp](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/rocdecode/src/rocdecode/vaapi/d3d12_interop.cpp) (both source read)
- Code comment: "Windows interop: D3D12 NV12/P016 surfaces are tiled on AMD, so each frame is GPU-copied (CopyTextureRegion) from the tiled decode texture into a linear staging buffer, which is then imported into HIP. The HIP import is set up once per surface slot on the first frame and reused."
- Staging buffers are `CreateCommittedResource(D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_SHARED, buffer)`, followed by `CreateSharedHandle(..., GENERIC_ALL, ...)`.
- Import uses `type = hipExternalMemoryHandleTypeD3D12Resource`, `handle.win32.handle`, and `size`. **No Dedicated flag is set** here. That differs from OIDN and Orochi, and it still works because the front-end accepts flags = 0.
- rocDecode keeps the NT handle open until teardown, then calls `hipFree(mapped)`, `hipDestroyExternalMemory`, and `CloseHandle`.
- Sync is a **CPU wait** on a non-shared D3D12 fence (`Signal`, `SetEventOnCompletion`, `WaitForSingleObject`), not an imported fence.
- For decode textures it tries `D3D12_TEXTURE_LAYOUT_ROW_MAJOR` with `D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER` and `D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER`. If that fails ("e.g. on discrete GPUs"), it falls back to `LAYOUT_UNKNOWN` (tiled).

**Known pitfalls (AMD's porting notes)** — [amd/skills fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)
- "External memory ports 1:1; external semaphores do not."
  - `hipImportExternalSemaphore` "rejects the timeline handle types (`TimelineSemaphoreFd`, `TimelineSemaphoreWin32`) in a front-end range check".
  - Importing a Vulkan timeline semaphore as OpaqueWin32 "has been reported to let HIP read a buffer before Vulkan finished writing it (gfx1102, about 1 run in 25 ...; ROCm/hip#3909)".
  - The suggested GPU-ordered route: "create the fence in D3D12 (`CreateFence` + `CreateSharedHandle` ...) and import it into HIP as `hipExternalSemaphoreHandleTypeD3D12Fence`". It is described as "reported stable but is untested here".
  - "HIP has no `cudaDevAttrTimelineSemaphoreInteropSupported`."
- "Match HIP and Vulkan devices by PCI location (Linux) or LUID (Windows), never by UUID. HIP's UUID is the board serial."
- Windows submission batching: Windows "loses this race deterministically -- its KMD batches/defers submission". A missing stream ordering between a tiny upload and its consumer shows up only on Windows (gfx1201 among the affected archs). "`hipFree` is synchronizing (`hipFreeAsync` is not)".

**No D3D12 test found in hip-tests**
- A GitHub code search for `hipExternalSemaphoreHandleTypeD3D12Fence` and `hipExternalMemoryHandleTypeD3D12Resource` found OIDN, Orochi, HIPRT's Orochi copy, rocDecode, intel/llvm unified-runtime's HIP adapter and third-party probes, but **no hip-tests file**. — GitHub code search via `gh search code` (2026-11-18)

### Inferences
- **The recipe for EV** is the same on both DLLs. Only the prefix changes (`hip`/`cuda`):
  - **Buffers:**
    1. Shared committed buffers: `D3D12_HEAP_FLAG_SHARED`, a buffer desc with `ALLOW_UNORDERED_ACCESS`.
    2. `CreateSharedHandle`.
    3. Import as `D3D12Resource` with the Dedicated flag and size from `GetResourceAllocationInfo`.
    4. `CloseHandle` after the import. This is safe per Orochi's sample. rocDecode keeps the handle instead; both work.
    5. Map the whole range once.
  - **Fence:** one shared `ID3D12Fence` (`D3D12_FENCE_FLAG_SHARED`) imported as `D3D12Fence`, with monotonically increasing values. This is exactly what the CUDA doc in `optix_cuda_interop.md` prescribes. On AMD, **never use timeline, keyed-mutex or D3D11 fence types**.
  - **Device selection:** pick the HIP device by matching `hipDeviceProp_t::luid` (HIP ≥ 6, Windows) to `DXGI_ADAPTER_DESC1::AdapterLuid`, as OIDN does. Do not match by UUID.
- **Textures:** treat D3D12 textures as **not shareable as linear memory** on AMD. rocDecode explicitly copies tiled textures into linear buffers, as Falcor does on NVIDIA.
  - `hipExternalMemoryGetMappedMipmappedArray` exists in the source, but I found no Windows D3D12-texture example.
  - Write HLSL compute passes that copy texture to shared buffer and back, which is what the denoiser glue needs anyway.
- **Placed resources in a shared heap** (`D3D12Heap` import) are accepted by the front-end and advertised by OIDN, but none of the three working examples uses them. Start with committed resources.
- **OIDN capability vs HIP reality:** OIDN advertises TimelineSemaphoreWin32, KeyedMutex and D3D11Fence for HIP on Windows, but the HIP front-end range check (types > 4 rejected) means those imports would fail with `hipErrorInvalidValue`. This is an inference from reading both sources, not from a test. Through OIDN on AMD, use `OIDN_EXTERNAL_SEMAPHORE_TYPE_FLAG_D3D12_FENCE` only.
- **Windows ordering:** since the Windows KMD batches submissions, always put the HIP work on one explicit `hipStream_t` (not the null stream). Order it only through the imported fence, and never rely on timing.
- **Fence ordering on the CPU:** the CPU must issue the HIP signal before the D3D12 `Wait` that depends on it, and the D3D12 signal before the HIP wait (same as CUDA).

### Gaps
- I found no AMD document page that describes D3D12 interop (memory or fences) on Windows. All evidence is source code and third-party use.
- The Windows HIP runtime is "closed source" per the component table. The open `clr` code read here is the shared front-end plus the PAL backend. That the shipped `amdhip64_7.dll` behaves identically is very likely, but not provable from the docs.
- I found no hip-tests coverage for D3D12. Minimum driver for D3D12 import: OIDN says only "recent AMD GPU drivers" (2.3.1); OIDN's README minimum is Adrenalin 25.30.02.01.
- No measured cost of a D3D12 to HIP fence round trip on RDNA4 was found.

---

## 3. Orochi (GPUOpen) and how Blender Cycles shares one kernel source between CUDA and HIP

### Takeaway
**Orochi** (MIT, AMD GPUOpen, actively maintained: commits through 2026-10-29) is a single library with an `oro*` API that dynamically loads either `amdhip64_*.dll` or `nvcuda.dll` at runtime, so one binary can run on both vendors. Kernels are usually compiled at runtime through `orortc` (hipRTC or NVRTC) from one source file, optionally from precompiled bitcode. It wraps the external-memory/semaphore API and ships a working D3D12 interop sample and an RDNA4 WMMA sample. HIP RT vendors it.

**Blender Cycles** does not use Orochi. It shares one kernel source tree (`kernel/device/gpu/kernel.h`) and adds a thin per-backend `compat.h` / `config.h` / `globals.h`. It precompiles per-arch fatbins/cubins offline and loads them through its own hipew/cuew.

For EV, the **two-DLL approach with a portability header** is closest to Blender's model and keeps nvcc/hipcc offline compilation with full error checking. Orochi is the alternative if you want one DLL and runtime compilation.

### Cited Findings
**Orochi**
- "Orochi is a library loading HIP and CUDA APIs dynamically, allowing the user to switch APIs at runtime." It "combines the functionality offered by HIPEW and CUEW into a single library." "This library doesn't require you to link to CUDA nor HIP at build-time." APIs have the prefix `oro`, e.g. `oroInitialize( ORO_API_HIP, 0 ); oroInit( 0 );`. — [Orochi README](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi)
- Build and versions:
  - premake (`./tools/premake5/win/premake5.exe vs2022`);
  - the CUDA backend needs the `OROCHI_ENABLE_CUEW` define and the CUDA include path;
  - `--precompiled` enables precompiled bitcode;
  - `main` compiles against "a CUDA 12.x or a CUDA 13.x SDK", and CUDA 13 needs an r580+ driver;
  - release branches `release/hip5.7_cuda12.2` and `release/hip6.0_cuda12.2`.
  - License: MIT.
  - — [Orochi README](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/Orochi/main/README.md); [Orochi repo page](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi)
- Runtime behavior (`Orochi.cpp`):
  - `oroInitialize` tries CUDA (cuew) first, then HIP (hipew), and returns `ORO_ERROR_OPEN_FAILED` if neither loads.
  - `orortcGetBitcode` dispatches to `nvrtcGetCUBIN` or `hiprtcGetBitcode`.
  - Interop wrappers exist: `oroImportExternalMemory`, `oroExternalMemoryGetMappedBuffer`, `oroExternalMemoryGetMappedMipmappedArray`, `oroImportExternalSemaphore`, `oroDestroyExternalMemory/Semaphore`.
  - `oroWait/SignalExternalSemaphoresAsync` are used in the D3D12 sample.
  - `Orochi.h` defines `oroExternalMemoryHandleTypeD3D12Resource` = `hipExternalMemoryHandleTypeD3D12Resource`. `nvidia_hip_runtime_api_oro.h` maps hip names to cuda names.
  - — [Orochi/Orochi.cpp](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/blob/main/Orochi/Orochi.cpp); [GitHub code search results for D3D12Resource](https://github.com/search?q=hipExternalMemoryHandleTypeD3D12Resource&type=code)
- Tests and samples: `DeviceEnum`, `RadixSort`, `SimpleD3D12`, `Texture`, `VulkanComputeSimple`, `WMMA`. — [Orochi Test directory](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/tree/main/Test)
- The WMMA test compiles the kernel at runtime with `o.getFunctionFromFile(device, "../Test/WMMA/wmma_test_kernel.h", "wmma_matmul", nullptr)`, then launches it with `oroModuleLaunchKernel(function, 1,1,1, 32,1,1, 0, 0, args, 0)` (one wave32). — [Orochi Test/WMMA/main.cpp](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/Orochi/main/Test/WMMA/main.cpp)
- Maintenance:
  - Commits 2026-09-24 (hipRTC 7.14–7.16 DLL names on Windows), 2026-10-16 (premake 5.0.0, simpleD3D12 clang-cl fixes) and 2026-10-29 (merge).
  - Branches include `feature/ORO-0-cuda13`, `feature/ORO-0-hip7_13` and `feature/ORO-0-interop`.
  - — [Orochi commits](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/commits/main); [Orochi branches](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/branches)
- Users: HIP RT vendors a copy at `HIPRT/contrib/Orochi`, and so does a miner project (`tnn-miner/contrib/Orochi`). — [GitHub code search results](https://github.com/search?q=oroExternalMemoryHandleTypeD3D12Resource&type=code)
- The GPUOpen RDNA4 matrix-core article's full host program uses Orochi. — [GPUOpen: Using the Matrix Cores of AMD RDNA 4](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/)

**Blender Cycles kernel sharing**
- `intern/cycles/kernel/device/hip/kernel.cpp` is only `#ifdef __HIP_DEVICE_COMPILE__` plus these includes:
  - `kernel/device/hip/compat.h`, `config.h`, `globals.h`;
  - then the shared `kernel/device/gpu/image.h` and `kernel/device/gpu/kernel.h`.
  - — [Blender hip/kernel.cpp](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/kernel.cpp)
- HIP `compat.h`:
  - under `__HIP__` it includes `hip/hip_fp16.h` and `hip/hip_runtime.h`; under `__HIPCC_RTC__` it skips `<stdint.h>`;
  - macros: `#define ccl_device __device__ __inline__`, `#define ccl_global` (empty), `#define ccl_gpu_shared __shared__`, `#define ccl_gpu_warp_size (warpSize)`, `#define ccl_gpu_thread_mask(thread_warp) uint64_t((1ull << thread_warp) - 1)`, `#define ccl_gpu_ballot(predicate) __ballot(predicate)`, `#define ccl_gpu_syncthreads() __syncthreads()`;
  - fast-math remaps such as `#define cosf(x) __cosf(((float)(x)))`.
  - — [Blender hip/compat.h](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/compat.h)
- HIP `config.h`:
  - `GPU_MULTIPRESSOR_MAX_REGISTERS 65536`, `GPU_MULTIPROCESSOR_MAX_BLOCKS 64`, `GPU_BLOCK_MAX_THREADS 1024`, `GPU_THREAD_MAX_REGISTERS 255`, `GPU_KERNEL_BLOCK_NUM_THREADS 1024`, `GPU_KERNEL_MAX_REGISTERS 64`;
  - `ccl_gpu_kernel(...)` expands to `extern "C" __global__ void __launch_bounds__(...)`.
  - — [Blender hip/config.h](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/config.h)
- Kernels are compiled offline per arch with `hipcc --genco` into zstd-compressed fatbins and loaded with `hipModuleLoadData`. HIP and HIP RT have separate CMake trees. — [Blender kernel/device/hip/CMakeLists.txt](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/CMakeLists.txt); [Blender device/hip/device_impl.cpp](https://github.com/blender/blender/blob/main/intern/cycles/device/hip/device_impl.cpp)
- Blender loads HIP dynamically through its own `extern/hipew` (`hipModuleLoad`, `hipModuleLoadData` function pointers), not Orochi. — [Blender extern/hipew/src/hipew.c](https://github.com/blender/blender/blob/main/extern/hipew/src/hipew.c)

### Inferences
- **Two-DLL approach (EV-Cuda.dll / EV-Hip.dll) vs Orochi**

  | | Two DLLs + portability header (Blender-like) | Orochi (one DLL, runtime switch) |
  |---|---|---|
  | Kernel compile | Offline: nvcc (sm_86) / hipcc (gfx1201). Errors at build time, fatbins are ready | Usually runtime via hipRTC/NVRTC from source (first-launch cost, needs versioned rtc DLLs present). Precompiled bitcode is possible |
  | Host API | `cuda*` / `hip*` through a macro header | `oro*` everywhere, so the student learns a third API |
  | Debug/profile | Nsight on NVIDIA, ROCgdb/RGP on AMD | Same tools, but runtime-compiled code has less debug info by default (inference) |
  | Libraries (cuBLAS/hipBLAS etc.) | Normal | Not wrapped (Orochi wraps the driver API and rtc only; inference from README scope) |
  | Teaching value | Shows exactly where the vendors differ | Hides the differences |

- For a student, the **two-DLL approach** is the better default.
  - Copy Blender's structure: `ev_gpu/kernels/*.h` holds the shared device code; `cuda/compat.h` and `hip/compat.h` hold macros (`EV_DEVICE`, `EV_SHARED`, `EV_WARP_SIZE`, ballot/mask types).
  - Each backend has one `kernel.cu` / `kernel.hip` translation unit that includes the shared code.
  - The interop code is identical apart from prefixes, which Orochi's SimpleD3D12 proves on both vendors.
- Orochi is still useful as a **reference** (SimpleD3D12, WMMA, DLL-name lists). It is also a fallback if the student later wants one EV-Gpu.dll that picks the vendor at runtime.

### Gaps
- Orochi has no tagged GitHub releases, and the README does not list "used by" projects beyond what code search shows. I could not confirm whether Blender ever adopted Orochi; current Blender uses hipew.
- I did not read Orochi's `OrochiUtils::getFunctionFromFile` to see the exact hipRTC options it passes (for example `--offload-arch`) or its caching.

---

## 4. HIP's NVIDIA backend on Windows, and the minimal portability header: warp size, masks, shuffles, LDS, half types

### Takeaway
HIP's NVIDIA path ("hipnv") is **header-only**: `nvidia_hip_runtime_api.h` `#define`s every `hip*` name to its `cuda*` name. It is compiled by nvcc. AMD does not document it on Windows. rocm-examples' "Portable" VS solutions and `ROCM_EXAMPLES_GPU_LANGUAGE=CUDA` show it is used for builds that target both vendors, but I found no official Windows support statement. So the small hand-written portability header (Blender style) is the safer choice.

The key device-code differences:
- `warpSize` is **32 on RDNA in HIP** (wave64 is unsupported for HIP on gfx10+) and is **not a host compile-time constant**;
- lane masks and `__ballot` / `__activemask` are **64-bit** in HIP;
- HIP has **no `nvcuda::wmma`** and no dynamic parallelism;
- `__launch_bounds__`' second argument means something different;
- LDS is 64 KB per workgroup on RDNA (128 KB per WGP), vs up to 99 KB per block (opt-in) on the RTX 3070.

### Cited Findings
**HIP on NVIDIA**
- hipother: "This repository provides files required to support non-AMD specific back-end implementation for HIP." "The hipother repository is retired, please use the ROCm/rocm-systems repository." No Windows statement. — [ROCm/hipother](https://github.com/ROCm/hipother)
- The current header `rocm-systems/projects/hipother/hipnv/include/hip/nvidia_detail/nvidia_hip_runtime_api.h` contains, for example, `#define hipExternalMemoryHandleTypeD3D12Resource cudaExternalMemoryHandleTypeD3D12Resource`. — [GitHub code search result](https://github.com/ROCm/rocm-systems/blob/HEAD/projects/hipother/hipnv/include/hip/nvidia_detail/nvidia_hip_runtime_api.h)
- rocm-examples: the top-level VS solutions come in two versions, one of which "contains the examples that support both ROCm and CUDA". The CMake option `ROCM_EXAMPLES_GPU_LANGUAGE` is `"CUDA"` for NVIDIA or `"HIP"` (default). The README does not say how CUDA builds work on Windows. — [rocm-examples README](https://raw.githubusercontent.com/ROCm/rocm-examples/amd-staging/README.md)
- The HIP install page covers AMD only. "The HIP runtime is included with the ROCm Core SDK on Linux and Windows." — [HIP install](https://rocm.docs.amd.com/projects/HIP/en/latest/install/install.html)

**Warp/wave size, masks, shuffles** — [HIP C++ language extensions](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html)
- "NVIDIA devices return 32 for this variable; AMD devices return 64 for gfx9 and 32 for gfx10 and above." "HIP doesn't support `warpSize` of 64 on gfx10 and above."
- `warpSize` "can not be assumed to be a compile-time constant on the host". Since ROCm 7.0 "it is early folded by the compiler, allowing it to be used in loop bounds". `-mwavefrontsize64` "is not supported by HIP runtime".
- "All mask values returned or accepted by these built-ins are 64-bit unsigned integer values." `__ballot` and `__activemask` "have a 64-bit return value". The `_sync` shuffle variants take a 64-bit mask, are enabled by default in ROCm 7.0, and can be disabled with `HIP_DISABLE_WARP_SYNC_BUILTINS`.
- "HIP does not support dynamic parallelism." "HIP does not support warp matrix types or functions."
- Hardware: the ROCm spec table lists "Wavefront Size: 32 or 64" for the RX 9070 XT. That is hardware capability; HIP uses 32. — [ROCm GPU hardware specifications](https://rocm.docs.amd.com/en/latest/reference/gpu-arch-specs.html)
- On ROCm 7.14+, `<amd_hip_bf16.h>` defines `__shfl_*_sync`. On 7.2.x, `__syncwarp` sits inside the same guard as the sync builtins, so defining `HIP_DISABLE_WARP_SYNC_BUILTINS` also removes `__syncwarp`. — [amd/skills fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)

**Porting rules** — [HIP porting guide](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_porting_guide.html)
- "Code should not assume a warp size of 32 or 64." Use a `lane_mask_t` that is `uint64_t` on `__GFX8__`/`__GFX9__` and `uint32_t` otherwise.
- `__HIP_DEVICE_COMPILE__` replaces `__CUDA_ARCH__`. `__HIPCC__` and `__HIP__` are defined for `.hip` files or `-x hip`. `__HIP_PLATFORM_AMD__` is defined by amdclang++.
- `__launch_bounds__`: the first parameter is the same. "`MIN_BLOCKS_PER_MULTIPROCESSOR` in CUDA must be converted to `MIN_WARPS_PER_EXECUTION_UNIT` in HIP", dividing by `warpSize*4` in WGP mode (RDNA). `--maxregcount` is not supported.

**LDS / shared memory**
- RX 9070 XT row: 64 CUs, "LDS (KiB) 128", VGPR file 768 KiB, SGPR 32 KiB, L2 8 MiB, Infinity Cache 64 MiB, L0 vector 32 KiB, GFXIP 12.0. — [ROCm GPU hardware specifications](https://rocm.docs.amd.com/en/latest/reference/gpu-arch-specs.html)
- LDS has 64 banks of 4 bytes and 256 bytes/cycle on RDNA 2, 3 and 4. RDNA's primary mode "uses 32-thread warps". A WGP is "two closely coupled compute units sharing resources". — [HIP hardware implementation](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html)
- The RDNA architecture deck (2019, RDNA1) describes 128 KB LDS per WGP and up to 64 KB per workgroup (seen via search snippet only). — [RDNA Architecture public deck](https://gpuopen.com/download/RDNA_Architecture_public.pdf)
- A FlyDSL PR reports HIP `sharedMemPerBlock = 65536` with 128 KB physical LDS per WGP, but for gfx1151, not gfx1201 (search snippet only). — [ROCm/FlyDSL PR 567](https://github.com/ROCm/FlyDSL/pull/567)
- CUDA compute capability 8.6: 100 KB max shared memory per SM and 99 KB per block (opt-in above 48 KB); 64 K 32-bit registers per SM, 255 per thread, 1024 threads per block, warp size 32. — [CUDA Programming Guide: compute capabilities](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)

**Half / bf16 / fp8 types** — [HIP low precision floating point types](https://rocm.docs.amd.com/projects/HIP/en/latest/reference/low_fp_types.html)
- `<hip/hip_fp16.h>`: `__half`, `__half2`. `<hip/hip_bf16.h>`: `__hip_bfloat16`, `__hip_bfloat162`. `<hip/hip_fp8.h>`: `__hip_fp8_e4m3`, `__hip_fp8_e5m2` (OCP) and the `_fnuz` variants.
- OCP FP8 is native on CDNA4 and **RDNA4**. FNUZ is native on gfx94x only.
- hipRTC "declares `half`, `__hip_bfloat16` and the fp16/bf16 intrinsics itself, but `#include <hip/hip_fp16.h>` fails without `-I<rocm>/include`". — [amd/skills fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)

### Inferences
- **Minimal `ev_gpu_compat.h`.** This sketch is assembled from the facts above; it is not copied from one source.
  ```cpp
  #if defined(__HIPCC__)                 // EV-Hip.dll, hipcc/clang++
    #include <hip/hip_runtime.h>
    #include <hip/hip_fp16.h>
    #define gpuMalloc              hipMalloc
    #define gpuFree                hipFree
    #define gpuStream_t            hipStream_t
    #define gpuImportExternalMemory hipImportExternalMemory   // ...etc for the ~15 calls used
    typedef uint64_t ev_lanemask_t;      // HIP masks/ballot are 64-bit even in wave32
    #define EV_BALLOT(p)           __ballot(p)
  #elif defined(__CUDACC__)              // EV-Cuda.dll, nvcc
    #include <cuda_runtime.h>
    #include <cuda_fp16.h>
    #define gpuMalloc              cudaMalloc
    // ...
    typedef uint32_t ev_lanemask_t;
    #define EV_BALLOT(p)           __ballot_sync(0xffffffffu, p)
  #endif
  #define EV_WARP 32                     // both targets are wave32/warp32; static_assert at init via device props
  ```
- **Rules for kernel code shared by both DLLs:**
  - Write it for warp 32, but assert at startup that `hipDeviceProp_t::warpSize == 32`.
  - Never hard-code `0xffffffff` masks; go through `ev_lanemask_t`.
  - Keep shared memory per block at **≤ 48 KB** so the code works on both without opt-in. AMD tops out at 64 KB per workgroup (verify with `sharedMemPerBlock`). Going to 99 KB on the 3070 needs `cudaFuncSetAttribute(..., cudaFuncAttributeMaxDynamicSharedMemorySize, ...)`, which has no AMD equivalent beyond 64 KB.
  - Use `__launch_bounds__(N)` with one argument only.
  - Use `__half` from each vendor's fp16 header. The names match, so a shared kernel can use `__half` and `__float2half` unchanged (inferred from matching type names; check the intrinsic coverage).
  - Atomics (`atomicAdd` on float/int) have the same spelling in HIP. I did not verify the performance of float atomics on RDNA4 LDS vs global memory.
  - Cooperative groups exist in HIP (`hip/hip_cooperative_groups.h`), but grid-wide sync semantics were not checked here. Avoid them in v1.
- **HIP on NVIDIA on Windows:** if the student wants to try it, the route is to include `hip/hip_runtime.h` with `__HIP_PLATFORM_NVIDIA__` defined and the hipnv headers on the include path, then compile with nvcc. It is undocumented on Windows, so treat it as an experiment, not the plan.

### Gaps
- There is no official statement either way on HIP_PLATFORM=nvidia support on Windows.
- The gfx1201 `sharedMemPerBlock` value (expected 65536) was not confirmed from an AMD document. Query it at runtime.
- HIP cooperative groups (grid sync) support on Windows/RDNA4 was not researched.

---

## 5. Matrix instructions on RDNA4 from HIP (WMMA builtins, rocWMMA), the CUDA counterpart on the RTX 3070, and small fused MLP examples

### Takeaway
On gfx1201, the matrix cores are reached from HIP through **`__builtin_amdgcn_wmma_<CD>_16x16x16_<AB>_w32_gfx12`** builtins (for example `__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12`).
- They work on 16x16x16 tiles per wave32, and each lane holds 8 elements of A, B and C/D.
- On RDNA4 **D's layout equals B's** (apart from the type), so MLP layers chain without lane shuffles. RDNA3 needed shuffles.
- **rocWMMA** (header-only, a `fragment`/`mma_sync`-style API) supports gfx1200/gfx1201 and is listed for Windows in ROCm 10.1 (rocWMMA 2.2.1). Its standalone repo is retired in favor of rocm-libraries.

HIP does **not** implement `nvcuda::wmma`. On the RTX 3070, use CUDA `nvcuda::wmma` (`<mma.h>`, fp16 m16n16k16 with fp32 accumulate) or `mma.sync` PTX. An `EV_MMA_16x16x16` abstraction in the portability header is the practical bridge.

No official AMD 64-wide fused MLP *training* kernel in HIP was found. What exists:
- AMD's **MiniDXNN** (HLSL cooperative vectors, inference; training "next release");
- a community **tiny-rdna4-nn** port (gfx1201, Linux, an inference-only width-64 rocWMMA MLP).

### Cited Findings
**WMMA builtins on gfx12** — [GPUOpen: Using the Matrix Cores of AMD RDNA 4 architecture GPUs](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/) (Harada and Yoshimura, 2025-07-11)
- Naming: "__builtin_amdgcn_wmma_<C, D format>_16x16x16_<A, B format>_w32_gfx12". The postfix `_gfx12` "did not exist for the WMMA intrinsics for RDNA 3 or gfx11".
- "Each lane only needs to load (or store) 8 elements of a matrix." "a wavefront consists of 32 lanes". The sample maps lanes with `threadIdx.x % 16` and `threadIdx.x / 16`. B, C and D are row-major and A is column-major in the article's layout figure.
- Sample: `extern "C" __global__ void wmma_matmul(__fp16* a, __fp16* b, __fp16* c)` with `c_frag = __builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(a_frag, b_frag, c_frag);`. The host is Orochi: [Orochi Test/WMMA](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/tree/main/Test/WMMA).
- MLP chaining: the output of one WMMA can feed the next without cross-lane exchange "since matrix D and B have the same layout, except for the floating-point data type". Downcasting is still needed. On RDNA3, lane shuffling was needed.
- A search-snippet summary noted the article text contradicts itself once about C/D precision. Trust the intrinsic name (`f32` C/D, `f16` A/B).
- GPUOpen "WMMA guide for AMD RDNA 4 architecture GPUs" series (Hui Zhang, AMD Devtech):
  - Part 1 (2026-06-02): fused GEMMs and transposing D by swapping A and B, using `__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12` with `_Float16` ext_vector types. "deployed in Llama.cpp to implement Flash Attention on RDNA 4". No TFLOPS numbers.
  - Part 2: fusing WMMA to double K, with 128-bit loads for FP8/INT8.
  - Part 3: fast in-register transpose.
  - — [WMMA guide part 1](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-1/); [part 2](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-2/); [part 3](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-3/)
- Builtin signature churn: LLVM PR #175039 (Jan 2026) "Modifies raytracing and wmma builtin def to take _Float16 for HIP/C++". — [cfe-commits PR #175039](https://lists.llvm.org/pipermail/cfe-commits/Week-of-Mon-20260105/793699.html)
- Composable Kernel wraps the gfx12 builtins under `#if defined(__gfx12__)`. — [CK amd_wmma.hpp source](https://rocm.docs.amd.com/projects/composable_kernel/en/latest/doxygen/html/amd__wmma_8hpp_source.html)
- OIDN classifies gfx1200 and gfx1201 as `HIPArch::WMMA` and has `ck_conv_wmma.cpp` (Composable Kernel WMMA convolutions). CK tensors "must be smaller than 2GB". — [oidn hip_device.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_device.cpp); [oidn devices/hip directory](https://github.com/RenderKit/oidn/tree/master/devices/hip); [oidn hip_engine.cpp](https://github.com/RenderKit/oidn/blob/master/devices/hip/hip_engine.cpp)

**rocWMMA**
- Supported: "RDNA class GPU featuring AI acceleration support: gfx1100, gfx1101, gfx1102, gfx1151 as 'gfx11'; gfx1200, gfx1201 as 'gfx12'". "The API consists of a header library." "The rocWMMA repository is retired, please use the ROCm/rocm-libraries repository." Samples include simple and performant GEMMs, DLRM, GEMV and hipRTC integration. — [ROCm/rocWMMA](https://github.com/ROCm/rocWMMA)
- ROCm 10.1.0 lists rocWMMA 2.2.1 for "Linux/Windows · Instinct/Radeon/Ryzen". — [ROCm 10.1.0 release notes](https://rocm.docs.amd.com/en/latest/about/release-notes.html)
- Data types: fp16 builtins are documented above. OCP FP8 is native on RDNA4 ([HIP low-precision types](https://rocm.docs.amd.com/projects/HIP/en/latest/reference/low_fp_types.html)). bf16 and iu8 WMMA variants exist by the naming pattern; the per-type list was not on the fetched page.

**CUDA side (RTX 3070, sm_86)**
- "HIP does not support warp matrix types or functions." — [HIP C++ language extensions](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html)
- CUDA Warp Matrix Functions: "all following functions and types are defined in the namespace nvcuda::wmma". `load_matrix_sync(fragment&, const T* mptr, unsigned ldm[, layout])` waits until all warp lanes have arrived. The example declares a/b/accumulator fragments, then calls `load_matrix_sync`, `mma_sync` and `store_matrix_sync` (search-result excerpt of the guide). — [CUDA C++ Programming Guide (search excerpt)](https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html)
- WMMA loads from shared memory expect 128-bit alignment. — [CUTLASS functionality doc](https://docs.nvidia.com/cutlass/4.6.2/media/docs/cpp/functionality.html)
- The CUDA compute-capability tables mark "Bfloat16-precision floating-point operations" as Yes for 8.x. Tensor Core access is described "through inline PTX" and libraries. — [CUDA Programming Guide: compute capabilities](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)

**Small-MLP examples on RDNA**
- MiniDXNN (GPUOpen, 2026-04-09):
  - "a library for multi-layer perceptron (MLP) inference built natively for HLSL and DirectX® 12";
  - uses matrix cores "through cooperative vector APIs";
  - "requires using AMD developer drivers and is supported on AMD Radeon™ RX 9000 Series";
  - "Training support is coming in our next release";
  - a related blurb mentions a v0.4.0 with D3D12 Linear Algebra (SM 6.10).
  - Repo: [GPUOpen-LibrariesAndSDKs/MiniDxNn](https://github.com/GPUOpen-LibrariesAndSDKs/MiniDxNn). — [GPUOpen: Introducing MiniDXNN](https://gpuopen.com/learn/minidxnn-mlp-library-for-dx12/)
- tiny-rdna4-nn: an "independent community port of NVlabs/tiny-cuda-nn".
  - Target `gfx1201`, validated on a Radeon AI PRO R9700, ROCm 7.2 (HIP 7.2.53211), Ubuntu 24.04.
  - `RocWMMAWidth64MLP` is an opt-in FP16 fused **inference-only** backend for exactly 64 inputs, 2×64 ReLU hidden layers and 64 outputs.
  - "no performance claim" is made.
  - — [Painter3000/tiny-rdna4-nn releases](https://github.com/Painter3000/tiny-rdna4-nn/releases)

### Inferences
- **NRC MLP on both vendors with one source:** define the core op once in the portability header:
  `ev_mma16(acc, a, b)`, where a/b are 16x16 fp16 tiles and acc is 16x16 fp32, one warp/wave32.
  - HIP: `__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12` on per-lane 8-element `_Float16` / `float` ext_vectors.
  - CUDA: `wmma::fragment<matrix_a,16,16,16,__half,row_major>`, etc., with `wmma::mma_sync`.

  The fragment-to-lane layouts **differ**. CUDA's is opaque; RDNA4's is documented. So keep load and store inside the abstraction (rocWMMA's `fragment` API is the off-the-shelf version on AMD) and never let kernel code index fragment elements directly.
- **Layer cost:** a 64-wide layer for a 16-row batch tile is 4×4 = 16 WMMA ops (K=64 as 4 steps of 16) per output 16×64 tile. On RDNA4, chaining layers in registers is possible because the D and B layouts match. On CUDA, the usual tiny-cuda-nn approach goes through shared memory between layers.
- **Path:** start with a **non-tensor-core reference MLP** (plain fp32 FMA, same code on both DLLs) for correctness. Then add the WMMA/rocWMMA path behind the same interface and compare outputs. Expect builtin signature changes across ROCm/LLVM versions (PR #175039).
- **HLSL alternative:** MiniDXNN and DX LinAlg (see `neural_radiance_cache.md` §4) are the vendor-neutral long-term route. HIP WMMA and CUDA WMMA are the route for learning and control now.

### Gaps
- No official AMD HIP example of a fused MLP **training** kernel (forward + backward + Adam) on RDNA4 was found.
- No measured WMMA TFLOPS for the RX 9070 XT from AMD was found in the fetched pages.
- The exact list of gfx12 WMMA variants (bf16, fp8/bf8, iu8, iu4, the `f16_16x16x16_f16` accumulate-in-half variant) was not on the fetched GPUOpen page. Check the LLVM `BuiltinsAMDGPU.def` or the RDNA4 ISA guide.
- I could not fetch the CUDA programming guide's Warp Matrix Functions section directly (page too long). The fp16 m16n16k16 / m32n8k16 / m8n32k16 shapes and the bf16/tf32 requirement of 8.0+ are from my own knowledge, not verified this session.
- tiny-rdna4-nn's Windows support is not stated.

---

## 6. OIDN 2.5.x on Windows with the RX 9070 XT: requirements, interop API, binaries, OIDN 3

### Takeaway
OIDN **2.5.1** (2026-08-18) is current. Its HIP device supports RDNA 2/3/3.5/4 and needs **Adrenalin 25.30.02.01 or newer** on Windows. It imports D3D12 memory with `oidnNewSharedBufferFromWin32Handle(device, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE | OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED, handle, NULL, byteSize)`. It imports D3D12 fences with `oidnNewSharedSemaphoreFromWin32Handle` plus `oidnSignalSemaphoresAsync` / `oidnWaitSemaphoresAsync` (HIP: Windows only, since 2.5.0). `oidnNewHIPDevice(&deviceID, &stream, 1)` lets OIDN run on *your* HIP stream, which is the simplest way to share a fence with your own HIP kernels. OIDN 3 (temporal) is not released as of November 2026.

### Cited Findings
- Requirements:
  - "AMD GPUs with RDNA 2, RDNA 3, RDNA 3.5, and RDNA 4 architectures".
  - "Windows: AMD Software: Adrenalin Edition 25.30.02.01 or newer". "Linux: Radeon Software for Linux version 25.30.1 or newer".
  - Build: "AMD ROCm (HIP SDK) v6.4.2 or newer", CMake 3.21+, Ninja/Make ("The Visual Studio generator is *not* supported"), `OIDN_DEVICE_HIP` (OFF by default).
  - The RT filter "is *not* temporally stable".
  - — [OIDN README](https://raw.githubusercontent.com/RenderKit/oidn/master/README.md)
- Releases (2026 unless noted):
  - 2.5.1 (18 Aug): fixed leaking the imported memory object on CUDA/HIP if the import fails.
  - 2.5.0 (02 Jun): "Added API for importing external semaphores from graphics APIs (e.g. Vulkan, Direct3D 12)". "Currently this is supported only by CUDA (Windows and Linux) and HIP (Windows only) devices". Added the `OIDN_EXTERNAL_MEMORY_TYPE_FLAG_DEDICATED` flag, "which must be combined with the handle type flag".
  - 2.4.1 (13 Jan): RDNA 3.5 GFX1152. 2.4.0 (09 Jan): ROCm 7 compilation.
  - 2.3.3: "Added AMD RDNA4 GPU support".
  - 2.3.1: HIP LUID and DX12/Vulkan import on Windows.
  - 2.3.0: `oidnIsHIPDeviceSupported`.
  - — [OIDN releases](https://github.com/RenderKit/oidn/releases)
- API, per the documentation:
  - `OIDNBuffer oidnNewSharedBufferFromWin32Handle(OIDNDevice device, OIDNExternalMemoryTypeFlags handleType, void* handle, const void* name, size_t byteSize);`
  - Flags: NONE, OPAQUE_FD, DMA_BUF, OPAQUE_WIN32, OPAQUE_WIN32_KMT, D3D11_TEXTURE(_KMT), D3D11_RESOURCE(_KMT), D3D12_HEAP, D3D12_RESOURCE, DEDICATED. "we recommend to always use dedicated allocations if possible."
  - Semaphores: `oidnNewSharedSemaphoreFromFD`, `oidnNewSharedSemaphoreFromWin32Handle`, `oidnRetainSemaphore`, `oidnReleaseSemaphore`, `oidnSignalSemaphoresAsync`, `oidnWaitSemaphoresAsync`, type `OIDNSemaphore`.
  - Semaphore flags: NONE, OPAQUE_FD, OPAQUE_WIN32(_KMT), D3D11_FENCE, D3D12_FENCE, KEYED_MUTEX(_KMT), TIMELINE_SEMAPHORE_FD/WIN32. Support "is not guaranteed on all device types, driver versions and OSs"; query the `externalSemaphoreTypes` device parameter.
  - Devices: `oidnNewDeviceByLUID(const void* luid)`, with the warning that "some graphics drivers may even report mismatching UUIDs or LUIDs". `oidnNewHIPDevice(const int* deviceIDs, const hipStream_t* streams, int numPairs)`: "the current implementation supports only one pair". "A NULL stream corresponds to the default stream".
  - Performance: "Creating filter objects can be very expensive", so reuse filters. "We recommend using balanced or fast quality for interactive and real-time use cases." Progress monitoring adds overhead on GPU devices.
  - — [OIDN documentation](https://www.openimagedenoise.org/documentation.html)
- Implementation details for HIP (D3D12Resource is forced to Dedicated; semaphores use `fence.value`) are in section 2. — [oidn devices/hip](https://github.com/RenderKit/oidn/tree/master/devices/hip)
- Binaries: downloads list 2.5.1, including `oidn-2.5.1.x64.windows.zip` ("precompiled for 64-bit"). The page does not say which device backends are inside. — [OIDN downloads](https://www.openimagedenoise.org/downloads.html)
- OIDN 3: CG Channel (2026-01-30) reported that it will add temporal denoising, that it was originally due at the end of 2025, and that it is now scheduled for the second half of 2026. A third-party auto-summary of a BCON26 talk claims a December beta and a Q1 2027 final release (unreliable, not Intel). No OIDN 3 release exists on GitHub as of 2026-11-18; latest is 2.5.1. — [CG Channel: OIDN 3 will support temporal denoising](https://www.cgchannel.com/2026/01/open-image-denoise-3-will-support-temporal-denoising/); [OIDN releases](https://github.com/RenderKit/oidn/releases)

### Inferences
- **Recommended EV setup on AMD:**
  1. Create your own `hipStream_t` in EV-Hip.dll.
  2. Create the OIDN device with `oidnNewHIPDevice(&dev, &stream, 1)`, so that OIDN filters and your own HIP kernels (texture-to-buffer glue, NRC) are ordered on **one stream**.
  3. Import the D3D12 fence once with `hipImportExternalSemaphore(D3D12Fence)` and do `hipWait...` / `hipSignal...` yourself around the whole HIP batch.
  4. Wrap the shared D3D12 buffers either as OIDN shared buffers (`oidnNewSharedBufferFromWin32Handle`) or by passing your already-mapped HIP pointers with `oidnNewSharedBuffer(device, ptr, size)`.

  That avoids depending on OIDN's semaphore path at all. Mirror it on NVIDIA with `oidnNewCUDADevice` and a `cudaStream_t`.
- If OIDN's semaphore API is used instead, use only `OIDN_EXTERNAL_SEMAPHORE_TYPE_FLAG_D3D12_FENCE` on AMD (see the section 2 inference on HIP's range check).
- Use the `BALANCED` quality for interactive work and `HIGH` for final frames. Create the filter once per resolution.
- **Performance:** no numbers found. Benchmark 1080p/1440p on both GPUs after warm-up.

### Gaps
- No published OIDN timing for the RX 9070 XT or RTX 3070.
- I could not confirm from the fetched pages whether the official Windows ZIP includes `OpenImageDenoise_device_hip.dll`. My recollection that the official binaries ship CPU, SYCL, CUDA and HIP device DLLs is unverified; check the ZIP contents.
- There is no official Intel statement in 2026-H2 on the OIDN 3 date.

---

## 7. Debugging and profiling HIP on Windows: ROCgdb, RGP, Radeon Developer Tool Suite, PIX, RRA

### Takeaway
- **ROCgdb on Windows exists but is narrow.** It is an optional component of the legacy HIP SDK (7.2.0+) and supports only gfx1200/gfx1201, so the RX 9070 XT qualifies. It requires the driver bundled with the SDK and does not support more than one AMD GPU. It is command-line, and host debug info must be DWARF.
- **RGP 2.7.1** (Radeon Developer Tool Suite 2026-10-02) profiles HIP and D3D12 on Windows 11 with RX 9000 support.
- **PIX** has a built-in AMD counters plugin with RX 9070/9070 XT support.
- The ROCm Core SDK 10.1 lists ROCgdb and the rocprof tools as Linux-only.

### Cited Findings
- ROCgdb on Windows:
  - "the `gfx120x` architectures" (gfx1200 and gfx1201) only.
  - Use "the driver included with the HIP SDK package"; install via the "AMD ROCm Debugger (ROCgdb)" option of HIP SDK 7.2.0+.
  - Compile with `hipcc example.cpp -o example.exe --offload-arch=gfx1201 -gdwarf -Wl,-debug:dwarf -O0`, then run `rocgdb example.exe`.
  - — [AMD ROCm Debugger for Windows](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/how-to/debugger-windows.html)
- 7.2.0 limitations:
  - "Configurations with more than one AMD GPU are not supported".
  - No core dumps and no Python scripting.
  - Signals are not passed to the inferior.
  - "HIPCC on Windows defaults to PDB/CodeView for host debug info, which ROCgdb does not support". The MSVC ABI is only partly supported for host debugging, but device-side debugging is not affected.
  - — [HIP SDK 7.2.0 release notes](https://rocm.docs.amd.com/projects/install-on-windows/en/docs-7.2/about/releasenotes.html)
- A search snippet of the 7.1.1 notes lists an intermittent TDR "may result in a system crash with error code 0x116" while debugging. — [HIP SDK 7.1.1 release notes](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/about/releasenotes.html)
- ROCm 10.1.0 lists ROCgdb as "Linux · Instinct/Radeon", and ROCprofiler-SDK / Compute Profiler / Systems Profiler as Linux. — [ROCm 10.1.0 release notes](https://rocm.docs.amd.com/en/latest/about/release-notes.html)
- RGP:
  - v2.7.1 supports "the explicit graphics APIs (DirectX12 and Vulkan), compute APIs (OpenCL and HIP)" on RX 9000/7000/6000/5000.
  - The Radeon Developer Panel captures profiles from HIP apps. Windows 11 is the supported OS for compute.
  - User debug markers are documented only for DX12/Vulkan.
  - — [RGP manual](https://gpuopen.com/manuals/rgp_manual/)
- RGP releases:
  - V2.7.1 (06 Oct), download `RadeonDeveloperToolSuite-2026-10-02-1872.zip`;
  - V2.7 (11 Jun): driver info for HIP/OpenCL profiles, and "Support for PIX native user markers in DirectX 12 profiles (requires the May Agility SDK)";
  - V2.6.1 fixed RDNA4 raytracing ISA view issues;
  - V2.4: pure compute profiling for DX12/Vulkan.
  - — [radeon_gpu_profiler releases](https://github.com/GPUOpen-Tools/radeon_gpu_profiler/releases)
- RGP has supported HIP profiling on Windows since RGP 1.14. — [GPUOpen RGP 1.14](https://gpuopen.com/learn/rgp_1_14/)
- PIX: "There are no extra installers or configuration steps required ... you will see the AMD counters when running PIX Analysis on an AMD GPU." The May 2025 PIX release added the RX 9070 and 9070 XT. — [GPUOpen: DX12 hardware counter profiling with PIX and the AMD plugin](https://gpuopen.com/directx12-hardware-counter-profiling-with-microsoft-pix-and-the-amd-plugin/); [PIX blog: hardware counters in GPU captures](https://devblogs.microsoft.com/pix/hardware-counters-in-gpu-captures/)
- DirectX Dump Files (Oct 2026) list RX 9000/RX 7000 support with PIX 2610.28. — [DirectX blog: DirectX Dump Files released](https://devblogs.microsoft.com/directx/directx-dump-files-are-now-released/)

### Inferences
- **Practical debug workflow on the AMD PC:**
  - Debug kernels with `printf` (HIP supports device printf) plus the CPU-reference comparison first. Use ROCgdb for hard device bugs.
  - **Disable the Ryzen iGPU** (BIOS, or Device Manager) while using ROCgdb, because "more than one AMD GPU" is unsupported. Most Ryzen 7000/9000 desktop CPUs have an RDNA2 iGPU. This is an inference from the limitation; check whether the student's CPU has one.
  - Use RGP for both D3D12 frames and HIP dispatches. Use PIX (with the AMD plugin) for the D3D12 side.
  - On NVIDIA, use Nsight Systems for the CUDA + D3D12 timeline (see `optix_cuda_interop.md` §4).
- Whether one RGP capture shows D3D12 and HIP work of the same process on one timeline is undocumented. Plan to profile them separately and measure the fence wait with CPU timestamps or D3D12 timestamp queries.

### Gaps
- Radeon Raytracing Analyzer (RRA) for DXR on RDNA4 was not researched this session. It is part of the Radeon Developer Tool Suite, but no RRA page was fetched.
- No VS Code or Visual Studio integration of ROCgdb on Windows is documented; the VS Code guide is Linux-oriented.
- Whether HIP kernels profiled in RGP get instruction-level timing (as D3D12 shaders do) was not stated in the manual excerpt.

---

## 8. Learning resources for HIP and how directly CUDA material (PMPP) transfers

### Takeaway
HIP is close enough to CUDA that *Programming Massively Parallel Processors* (PMPP) and the CUDA guide transfer almost 1:1 for the programming model: threads/blocks/grids, shared-memory tiling, reductions, scans and streams. What does **not** transfer:
- warp-size assumptions (fine here since both targets are 32, but masks are 64-bit in HIP);
- `nvcuda::wmma` and Tensor Core chapters (use WMMA builtins or rocWMMA);
- `__launch_bounds__` minBlocks;
- shared memory above 64 KB;
- CUDA-only libraries and tools (Nsight vs RGP/ROCgdb).

The best HIP-specific material is AMD's HIP docs (language extensions, porting guide, hardware implementation), **rocm-examples** (VS solutions, a portable ROCm+CUDA subset, Vulkan/OpenGL interop), GPUOpen's RDNA4 matrix-core and WMMA series, Orochi's samples, and AMD's CUDA-to-ROCm fault notes.

### Cited Findings
- HIP C++ language extensions (warpSize, masks, shuffles, what is missing vs CUDA). — [HIP C++ language extensions](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_cpp_language_extensions.html)
- HIP porting guide (platform macros, lane masks, launch bounds). — [HIP porting guide](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_porting_guide.html)
- HIP hardware implementation (RDNA vs CDNA: wave32, WGP, LDS banks). — [HIP hardware implementation](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html)
- HIP external interop (Vulkan example; the closest official doc to the D3D12 pattern). — [HIP external interop how-to](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/hip_runtime_api/external_interop.html)
- rocm-examples:
  - categories HIP-Basic, HIP-Doc, Libraries, Applications, Systems, AI, Tutorials;
  - Windows VS 2017/2019/2022 solutions, including a ROCm+CUDA "portable" solution;
  - `HIP-Basic/vulkan_interop` and `HIP-Basic/opengl_interop`, and `matrix_multiplication` in HIP-Basic and Tutorials;
  - prerequisite ROCm 7.x.
  - — [ROCm/rocm-examples](https://github.com/ROCm/rocm-examples)
- GPUOpen RDNA4 matrix cores and the WMMA guide series. — [Using the Matrix Cores of RDNA 4](https://gpuopen.com/learn/using_matrix_core_amd_rdna4/); [WMMA guide part 1](https://gpuopen.com/learn/wmma-guide-amd-rdna-4-gpus-part-1/)
- Orochi samples: SimpleD3D12 (the D3D12 interop pattern on both vendors) and WMMA. — [Orochi Test](https://github.com/GPUOpen-LibrariesAndSDKs/Orochi/tree/main/Test)
- AMD's CUDA-to-ROCm porting fault catalogue (Windows CMake/HIP pitfalls, interop, stream-ordering races on Windows, hipRTC naming). — [amd/skills moat-cuda-to-rocm fault-classes.md](https://github.com/amd/skills/blob/HEAD/skills/moat-cuda-to-rocm/references/fault-classes.md)
- HIPIFY tables (CUDA to HIP API name mapping, with the HIP version that added each). — [HIPIFY CUDA Runtime API supported by HIP](https://github.com/ROCm/HIPIFY/blob/amd-staging/docs/reference/tables/CUDA_Runtime_API_functions_supported_by_HIP.md)
- PMPP 4th ed. (2022) details are already in `optix_cuda_interop.md` §4. — [Elsevier PMPP 4e](https://shop.elsevier.com/books/programming-massively-parallel-processors/hwu/978-0-323-91231-0)
- Blender Cycles' `kernel/device/{cuda,hip}` split is a production example of one kernel source with thin per-vendor compat headers. — [Blender hip/compat.h](https://github.com/blender/blender/blob/main/intern/cycles/kernel/device/hip/compat.h)

### Inferences
- **Suggested order for the student:**
  1. PMPP chapters 1–6 while writing **both** EV-Cuda and EV-Hip versions of each exercise. Run vector add, then tiled matmul, then reduction, then scan through the portability header from day one.
  2. The HIP porting guide and language-extensions page: one sitting, noting the five differences above.
  3. Orochi SimpleD3D12 and rocm-examples `vulkan_interop`, then the EV interop (shared buffer + fence).
  4. OIDN on both DLLs.
  5. GPUOpen matrix-core article and WMMA guide part 1, then the `ev_mma16` abstraction.
  6. NRC MLP (see `neural_radiance_cache.md` §7).
- For each PMPP exercise, profile with Nsight Compute on the 3070 and RGP on the 9070 XT. Comparing occupancy, wave32 vs warp32 behavior and LDS bank conflicts across vendors is itself a good learning goal.

### Gaps
- AMD "lab notes" / ROCm blog tutorials (rocm.blogs.amd.com) beyond the CDNA matrix-core post were not surveyed. Most target Instinct/Linux.
- No dedicated HIP textbook was found. PMPP plus the AMD docs is the practical combination.
- I found no official AMD tutorial for HIP + D3D12 interop. Orochi's SimpleD3D12 and OIDN's source are the references.
