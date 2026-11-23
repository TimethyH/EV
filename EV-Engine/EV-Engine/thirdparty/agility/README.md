# DirectX 12 Agility SDK 1.620.2 (vendored, trimmed)

Source: NuGet package `Microsoft.Direct3D.D3D12` **1.620.2** (`D3D12_SDK_VERSION 620`), downloaded 2026-11-19 from
https://www.nuget.org/packages/Microsoft.Direct3D.D3D12/1.620.2

What was kept (path tracing decision D3, milestone M0):

| Folder | From the package | Used for |
|---|---|---|
| `include/` | `build/native/include/*.h, *.hpp` (no `.idl`, no `d3dx12/` – EV has its own d3dx12) | Must come **before** `$(IncludePath)` so the Agility `d3d12.h` wins over the Windows SDK one |
| `redist/` | `build/native/bin/x64/D3D12Core.dll`, `d3d12SDKLayers.dll` (no `.pdb`, no tools) | Copied post-build to `<exe dir>\D3D12\` |

`redist/` is flat (not `bin/x64/`) because EV's `.gitignore` ignores `bin/` and `x64/` folders.

Both DLLs are on the package's "distributable files" list (`D3D12Core.dll`, `d3d12SDKLayers.dll`, `d3dconfig.exe`).
Licenses: `LICENSE.txt`, `LICENSE-CODE.txt`.

To upgrade: replace both folders from the new package and change the version number passed to
`EV_AGILITY_SDK_EXPORTS` (`header/core/agility_sdk.h`) – the number must match the DLL, otherwise device creation
fails with `D3D12_ERROR_INVALID_REDIST`.
