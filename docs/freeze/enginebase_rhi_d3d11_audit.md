# EngineBase/RHI_D3D11 local freeze audit

Audit scope: Goal 2.8 from `Epidemic_Engine_Full_Freeze_Plan_2026-09-14.md`.

Module: `EngineBase/RHI_D3D11`

Target: `EpidemicRHI_D3D11`

Result: `LOCAL_READY` for module-local D3D11 correctness. Cross-module lifetime/concurrency/load qualification remains Goals 6-9.

## Reviewed ownership and state

The module owns only transient D3D11/DXGI backend state. `D3D11DeviceState` owns COM references to `ID3D11Device` and `ID3D11DeviceContext`, the selected feature level, weak active-swap-chain binding, and terminal device-loss status/reason. Each `D3D11RhiSwapChain` owns its DXGI swap chain, current back buffer, RTV, logical descriptor/dimensions and recreate-required flag. Each `D3D11RhiCommandContext` owns only its logical frame-active flag plus shared device state.

All native COM resources use `Microsoft::WRL::ComPtr`. Device, context and swap-chain child objects share `D3D11DeviceState`, so destroying the public device wrapper cannot dangle already-created child objects. Active presentation binding is weak and cannot retain a destroyed swap chain. There is no persistence, ID/generation/revision/cursor space, secondary index, journal or gameplay-owned state.

## Public factory contract

| Callable ID | Contract |
|---|---|
| `37b182fdc4b11948` | `CreateD3D11RhiDevice`: validates `RhiDeviceDesc`, requests a hardware D3D11 device and immediate context, retries without feature level 11.1 only for the documented `E_INVALIDARG` compatibility case, and publishes `IRhiDevice` only after full native acquisition. Any partial COM output is reset before retry/failure. Optional debug validation requests `D3D11_CREATE_DEVICE_DEBUG`; missing SDK debug components return `rhi.d3d11.debug_layer_unavailable` rather than silently creating a non-debug device. Allocation failure follows the cross-engine `std::bad_alloc` policy. |

The module exposes no concrete D3D11 public class. Upper modules receive only `IRhiDevice`, `IRhiCommandContext` and `IRhiSwapChain`; therefore a D3D11 downcast cannot be required by the public contract.

## Backend lifecycle and failure atomicity

Device creation owns candidate native outputs in local `ComPtr` state until the public wrapper is created. Test-only fault injection after successful `D3D11CreateDevice` forces the same early-return shape and a subsequent factory call succeeds, proving the candidate cleanup path remains usable.

Swap-chain creation validates a live Win32 `HWND` before DXGI `CreateSwapChain`. The active presentation binding changes only after `D3D11RhiSwapChain::Create` fully succeeds. A test-only `GetBuffer` failure occurs after native swap-chain creation and proves destruction of the failed candidate permits an immediate successful retry.

`Resize` rejects zero width/height before calling DXGI. For a real resize, old RTV/back-buffer references are unbound and released before `ResizeBuffers`. After successful `ResizeBuffers`, logical dimensions move to the new buffers and a new back buffer/RTV is acquired. Failure to recreate the RTV sets `recreate_required`; both `Present` and `Clear` reject the incomplete target with `rhi.d3d11.recreate_required`. A later valid `Resize` recreates the RTV and restores clear/present operation. No stale RTV can remain published.

Swap-chain destruction unbinds and releases back-buffer resources, then COM member destruction releases the DXGI swap chain. The shared device/context outlives the device wrapper while referenced by surviving contexts or swap chains. Destroying the active swap chain expires the weak binding; subsequent `Clear` returns `rhi.d3d11.no_swap_chain` instead of dereferencing stale state.

## Device-lost and debug-layer policy

`DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET` and `DXGI_ERROR_DRIVER_INTERNAL_ERROR` are normalized to stable error code `rhi.d3d11.device_lost`. The first observed device-loss HRESULT records `ID3D11Device::GetDeviceRemovedReason()` when available. Later device/context/swap-chain native work is rejected through controlled `Result` failures. Logical `EndFrame` may still close an already-active frame without issuing further native commands.

Debug validation is a separate path. When requested and available, the created device preserves `enable_debug_validation=true`; when the SDK debug component is absent, factory returns `rhi.d3d11.debug_layer_unavailable`. There is no silent fallback to a non-debug device.

## Goal 2.8 evidence

1. Factory failure cleanup: `D3D11FactoryCleanupAndDebugLayer` injects failure immediately after native device/context acquisition, then creates another device successfully. Explicit `ComPtr::Reset` also clears any outputs before the feature-level retry and terminal factory failure.
2. Device/context lifetime: `D3D11SwapChainRecoveryAndLifetime` destroys the `IRhiDevice` wrapper and verifies the already-created context and swap chain can still begin/clear/end/present through shared native state.
3. Swap-chain creation/destruction: the same test injects failure after native swap-chain creation but before back-buffer acquisition, retries successfully, then destroys the active swap chain and verifies the command context sees `rhi.d3d11.no_swap_chain`.
4. RTV recreation: a forced `CreateRenderTargetView` failure after successful `ResizeBuffers` leaves no stale render target; `Present` and `Clear` both report `rhi.d3d11.recreate_required`, and retrying `Resize` restores rendering.
5. Minimize/restore: zero-area resize is rejected before DXGI, old usable dimensions and presentation remain valid, and a later non-zero resize is the restore/recreation path.
6. Present/device-lost/errors: native HRESULTs are returned as controlled operation errors; device-loss HRESULTs are promoted to stable `rhi.d3d11.device_lost` and block later native work.
7. Debug layer: `D3D11FactoryCleanupAndDebugLayer` covers both supported debug-device creation and the controlled `rhi.d3d11.debug_layer_unavailable` outcome.
8. No concrete downcast: the only public D3D11 header exports `CreateD3D11RhiDevice` returning `std::shared_ptr<IRhiDevice>`; concrete device/context/swap-chain classes remain source-private.

## Defects found and fixed during the audit

`RHI-D3D11-001`: after `ResizeBuffers` succeeded but RTV recreation failed, `Clear()` silently returned success because `BindForRendering` and `ClearRenderTarget` treated missing render target as a no-op. Fixed by validating render-target readiness before clear and returning `rhi.d3d11.recreate_required`. Regression: forced RTV recreation failure followed by `Clear` rejection and successful resize retry.

`RHI-D3D11-002`: device-loss HRESULTs were flattened into generic present/resize/backend operation errors, so callers could not distinguish a recoverable swap-chain recreation state from a terminal lost device. Fixed by recording device-loss state/reason and returning stable `rhi.d3d11.device_lost` from subsequent native-entry points.

`RHI-D3D11-003`: non-null but invalid Win32 window handles reached DXGI `CreateSwapChain`. Fixed by requiring `IsWindow(hwnd)` before the DXGI call. Regression verifies `rhi.invalid_surface_handle` and preservation of the previous active target.

`RHI-D3D11-004`: debug-layer absence was reported as generic `rhi.d3d11.create_device_failed`. Fixed with dedicated `rhi.d3d11.debug_layer_unavailable`, while preserving strict debug-request semantics.

## Regression scope and environment boundary

`EpidemicRhiIntegrationTests` contains five named cases, including the real hidden-window D3D11 smoke plus factory cleanup/debug and swap-chain recovery/lifetime cases. The integration translation unit is also syntax-checked under GCC and Clang with C++20 warnings-as-errors. Native D3D11 execution passed in the project Windows 11/MSVC Debug and Release matrix; deterministic fault seams exercise failure paths without requiring a physical device-loss event.

## Corrective patch 2026-09-18

`RHI-D3D11-005`: once resize releases the old back-buffer/RTV, recreation is required even if the requested dimensions equal the newly committed logical dimensions. `recreate_required_` is now set before releasing the old target. This preserves the existing same-size retry regression after injected `CreateRenderTargetView` failure: `Present`/`Clear` reject the missing target and retrying `Resize` recreates it instead of taking the same-size no-op branch.
