# EngineBase

`EngineBase` — нижний стабильный слой Epidemic Engine. Он запускает приложение, предоставляет общие примитивы, управляет жизненным циклом, рабочими потоками и main-thread задачами, получает события Windows, формирует снимок ввода и открывает минимальную границу к графическому устройству. Это не весь движок и не место для систем мира, ресурсов или gameplay.

Слой построен как набор отдельных CMake-библиотек. Нижние библиотеки не знают о верхних, а исполняемый файл собирает нужную конфигурацию явно. `Application` владеет контейнером сервисов и реестром модулей, проводит Bootstrap и Initialize, затем выполняет фиксированные фазы кадра и завершает модули в обратном порядке. После успешной инициализации контейнер сервисов закрывается, поэтому долгоживущие зависимости регистрируются только в composition root.

`Foundation` содержит `Error`, `Result<T>`, идентификаторы, handles, пути и базовое время. `Memory` добавляет allocators и наблюдение за расходом памяти. `Diagnostics` предоставляет логирование, счетчики и profiling scopes. `Core` содержит `Application`, service container, module registry, event bus, task scheduler, main-thread dispatcher и frame phases. `Platform` реализует Windows runtime, окна и platform events. `Input` превращает эти события в неизменяемый снимок клавиатуры и мыши на кадр. `RHI` описывает только device, command context, swap chain, clear и present, а `RHI_D3D11` реализует эту границу для Direct3D 11. `Support` собирает типичные конфигурации и не является самостоятельной системой движка.

EngineBase работает только на Windows. Это осознанное ограничение проекта: platform layer использует Win32, основной графический backend — D3D11, а разработка не должна тратить время на поддержку платформ, для которых игра не выпускается.

EngineBase не содержит assets, resources, сериализацию состояния мира, streaming, scene graph, renderer, physics, animation, audio, NPC, квесты или gameplay. Эти обязанности начинаются в `EngineRuntime` и последующих слоях.

Публичные контракты EngineBase считаются стабильными. После freeze допустимы исправления ошибок, дополнительные реализации существующих interfaces и новые необязательные функции, не меняющие смысл текущего кода. Верхние слои не должны требовать изменения EngineBase только ради удобства собственной реализации.

Сборка и тесты:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Примеры запуска находятся в `Apps`: headless lifecycle, обычное окно, ввод и D3D11 clear-screen. Они являются smoke-сценариями, а не шаблонами gameplay-приложения.
