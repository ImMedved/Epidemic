# Документация Epidemic Engine

Начинать знакомство с проектом следует с корневого `README.md`, затем с [`architecture.md`](architecture.md).

Документация нижних слоев устроена одинаково:

```text
EngineBase/
    README.md
    using_enginebase.md
    modules/

EngineRuntime/
    README.md
    using_engineruntime.md
    modules/
```

README слоя объясняет его назначение и границы. `using_*` показывает практическое использование. В `modules/` находится документация конкретных библиотек и их contracts. Планы реализации, старые freeze-аудиты и история промежуточных архивов не являются пользовательской документацией проекта.

Проверяемый baseline-инвентарь всех production-модулей находится в [`freeze/module_dossiers.md`](freeze/module_dossiers.md). Он генерируется и проверяется скриптом `freeze/module_dossiers.py`; статус `BASELINE_INVENTORIED` описывает текущий contract surface, но сам по себе не означает `LOCAL_READY`.

Матрица ответственности, authoritative ownership, прямых межмодульных include-зависимостей и внешних SDK boundaries находится в [`freeze/architecture_ownership_matrix.md`](freeze/architecture_ownership_matrix.md). Ее актуальность и правила проверяет `freeze/architecture_ownership.py`.

Единый admission contract для статуса `LOCAL_READY` зафиксирован в [`freeze/local_ready_contract.md`](freeze/local_ready_contract.md). Машинный ledger `freeze/local_ready_ledger.json` хранит отдельные статусы и evidence anchors всех 37 критериев для каждого из 78 модулей; `freeze/local_ready_contract.py` запрещает неполный перевод модуля в `LOCAL_READY`.

Точный coverage index public callables находится в [`freeze/public_api_inventory.md`](freeze/public_api_inventory.md). `freeze/public_api_inventory.py` сохраняет overloads отдельными signature rows, оставляет неизвестные non-const API как `UNCLASSIFIED` и принимает contract/test evidence только из явно reviewed `freeze/public_api_anchors.json`, без поиска совпадений имен в tests.

Source-контракт freeze CI проверяется `freeze/ci_gate_contract.py`: четыре обязательных профиля, warnings-as-errors, все generated checks, negative self-tests, self-containment и CTest должны оставаться в `.github/workflows/architecture-freeze.yml`. Отрицательные архитектурные fixtures запускаются через `cmake/ArchitectureFreezeSelfTest.cmake`, а независимый reviewed corpus API scanner хранится в `freeze/fixtures/`.
