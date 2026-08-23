# Epidemic Engine

Epidemic Engine — Windows-only C++20 движок, построенный как несколько строго направленных слоев. Нижние слои предоставляют стабильные механизмы, верхние добавляют игровое значение и никогда не протягивают зависимости обратно вниз.

Текущая структура проекта:

```text
EngineBase
    ↓
EngineRuntime
    ↓
GameFramework
    ↓
Game
```

`EngineBase` содержит минимальную платформу исполнения: общие типы, память и диагностику, microkernel и lifecycle приложения, Win32 platform/windowing, raw input и минимальный RHI с D3D11 backend. `EngineRuntime` содержит независимые engine majors: assets/resources, persistence, time/environment, scene/world/streaming, renderer, physics, navigation, animation, audio и simulation. Между Runtime majors нет прямых зависимостей; их технические связи собирает только `EngineRuntime/Support`.

`GameFramework` должен содержать переиспользуемые gameplay-системы, а `Game` — конкретные правила, контент и конечный composition root.

Подробное устройство слоев описано в [`docs/architecture.md`](docs/architecture.md). Документация EngineBase находится в [`docs/EngineBase/README.md`](docs/EngineBase/README.md), практическое использование — в [`docs/EngineBase/using_enginebase.md`](docs/EngineBase/using_enginebase.md). Для Runtime аналогичные документы находятся в [`docs/EngineRuntime/README.md`](docs/EngineRuntime/README.md) и [`docs/EngineRuntime/using_engineruntime.md`](docs/EngineRuntime/using_engineruntime.md).

## Сборка

Из корня репозитория:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## Тесты

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Только Runtime:

```powershell
ctest --test-dir build -C Debug -R EpidemicRuntime --output-on-failure
```

## Smoke-приложения EngineBase

После Debug-сборки:

```powershell
.\build\EngineBase\Apps\HeadlessCoreApp\EpidemicHeadlessCoreApp.exe
.\build\EngineBase\Apps\WindowSmokeApp\EpidemicWindowSmokeApp.exe
.\build\EngineBase\Apps\InputSmokeApp\EpidemicInputSmokeApp.exe
.\build\EngineBase\Apps\RhiClearScreenApp\EpidemicRhiClearScreenApp.exe
```

Логи записываются в `logs/epidemic.log`.

## Главное правило зависимостей

Нижний слой никогда не зависит от верхнего. Внутри `EngineRuntime` один major не включает и не линкует другой major; взаимодействие между ними осуществляется через public contracts и adapters в `Support`. Gameplay-смысл не добавляется в EngineBase или Runtime ради удобства верхнего кода.

Текущая базовая платформа — Windows 11 / Win32 / D3D11. Cross-platform поддержка не является целью этого этапа.

## License

This project is licensed under the PolyForm Noncommercial License 1.0.0.

Non-commercial use, study, research, modification, and hobby projects are permitted under the terms of the license.

Commercial use requires a separate license from the copyright holder.

See [LICENSE](LICENSE) for the full license terms.