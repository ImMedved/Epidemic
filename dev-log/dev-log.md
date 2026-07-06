## О документе
В этот документ пишется каждый законченный шаг после подтверждения, что реализация правильная и стабильная.

### Start

& cmd /c '"D:\Programs\VisualStudio2026\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Users\Akku\AppData\Roaming\Python\Python314\site-packages\cmake\data\bin\cmake.exe" --build ".\build"'

### tests
ctest --test-dir build --output-on-failure

### smoke runtime

.\build\EngineBase\Apps\HeadlessCoreApp\EpidemicHeadlessCoreApp.exe

.\build\EngineBase\Apps\WindowSmokeApp\EpidemicWindowSmokeApp.exe
.\build\EngineBase\Apps\InputSmokeApp\EpidemicInputSmokeApp.exe
.\build\EngineBase\Apps\RhiClearScreenApp\EpidemicRhiClearScreenApp.exe

### logs
Get-Content .\logs\epidemic.log -Tail 100

### live logs
Get-Content .\logs\epidemic.log -Wait
### 2026-07-05
- Hardened Application shutdown: scheduler exceptions during Shutdown/WaitIdle are now logged and do not abort application shutdown.
- Refined Win32 close semantics: WM_CLOSE now emits WindowCloseRequested without destroying the window immediately; the app must accept the request via IWindow::Close().
- Documented and asserted WindowsPlatformRuntime as owner-thread/main-thread runtime for window creation, pumping, and destruction.
- Tightened Input baseline: unknown Win32 key codes are ignored, unknown mouse buttons no longer alias to Left.
- Split tests into real Unit / Integration / Regression runners with different test sets instead of running one shared main three times.
- Implemented Step 10 D3D11 baseline: added `EpidemicRHI_D3D11` as a real static library with D3D11 device creation, feature-level selection, DXGI swap-chain creation, render-target recreation on resize, Present, and HRESULT-to-Error conversion.
- Reworked `EpidemicRhiClearScreenApp` into a real smoke app: it creates a Win32 window, builds the D3D11 RHI device through the abstract RHI contracts, clears the backbuffer every frame, handles resize/minimize safely, and exits on close request.
