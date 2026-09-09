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
