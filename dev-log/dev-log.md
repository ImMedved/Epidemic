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