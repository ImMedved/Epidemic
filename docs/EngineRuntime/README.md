# EngineRuntime

`EngineRuntime` — стабильный слой системного мира между `EngineBase` и будущим `GameFramework`. Он предоставляет механизмы, необходимые большой бесшовной игре: каталог assets, управляемые resources, сериализацию и persistence, время и environment, scene и world records, streaming, renderer foundation, physics, navigation, animation, audio и budgeted simulation.

Runtime  предоставляет нейтральные контракты (без конкретной реализации объектов игры), через которые GameFramework сможет реализовать эти системы, не связывая их напрямую с platform, D3D11 и внутренними реализациями.

Модули создаются как service bundles. Writer interfaces отделены от query interfaces, операции над authoritative state используют команды, revisions и `Result<T>`, а объекты с изменяемым lifetime адресуются generation handles. Долгая работа выполняется порциями под `RuntimeBudget`. Внешние backends подключаются через dependencies; reference implementations нужны для тестов и базовой композиции, но не подменяют production backend неявно.

RuntimeFoundation содержит общие ID, время, budgets и spatial values. Assets описывает стабильную идентичность и расположение данных. Resources загружает payloads и управляет leases. Serialization дает versioned documents и migrations, а Persistence хранит долговечное состояние мира. Time и Environment формируют время, сезоны, погоду и состояние поверхностей. Scene описывает представление объектов в пространстве, а World — их логическое положение, реальность и persistence tier. Streaming меняет residency chunks под budget. Renderer, Physics, Navigation, Animation и Audio открывают backend-neutral boundaries. Simulation выполняет ограниченную по бюджету фоновую работу и публикует proposals вместо прямого изменения чужого состояния.

`Support` должен собрать эти системы в один runtime: создать реальные adapters, провести atomic composition, выполнять кадр в определенном порядке и корректно завершать все lifetimes. Его публичная документация в этом комплекте предварительная и должна быть заменена после завершения реализации.

Большинство majors считаются frozen. После freeze допускаются локальные исправления, новые backend implementations и расширения, не меняющие смысл существующих contracts. Gameplay не является причиной менять Runtime, пока задача решается adapter или верхним слоем.

Runtime работает поверх Windows-only EngineBase, но большинство его чистых contracts не зависят от Win32. Тесты modules должны запускаться изолированно и не маскировать нарушение dependencies через Support.
