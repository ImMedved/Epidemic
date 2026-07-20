# Memory

## Назначение

Memory дает единый наблюдаемый baseline выделения памяти. Он не пытается заменить все стандартные контейнеры собственными и не задает глобальную модель памяти для каждого будущего subsystem. Его задача — предоставить allocators, tags, budgets и статистику, которую можно подключать там, где стоимость памяти имеет значение.

## Публичная модель

`IAllocator` описывает выделение и освобождение блока с alignment и `AllocationTag`. `DefaultAllocator` использует обычный heap, а `TrackingAllocator` добавляет учет через `IMemoryTracker`. `IFrameAllocator` обозначает allocator с явным сбросом. `MemoryTracker` хранит usage, peak, количество операций и optional budget по каждому tag.

| Контракт | Роль |
|---|---|
| `AllocationTag` | категория памяти для учета |
| `IAllocator` | базовый allocate/deallocate port |
| `TrackingAllocator` | decorator, записывающий операции |
| `IFrameAllocator` | allocator с lifetime одного кадра/пакета |
| `IMemoryTracker` | учет, бюджеты и статистика |
| `MemoryTagStatistics` | immutable diagnostic snapshot |

## Владение и ошибки

Allocator не владеет объектами, созданными вызывающим кодом; он отвечает только за память. Параметры `bytes`, `alignment` и tag при освобождении должны соответствовать выделению. Tracker предназначен для diagnostics и budget control, а не для восстановления потерянного ownership.

## Интеграция

`RegisterEngineBase()` регистрирует `IMemoryTracker` как service. Upper layers могут читать статистику и устанавливать budgets, но не должны зависеть от concrete `MemoryTracker`.

## Ограничения и стабильность

Модуль не реализует garbage collector, object pool для gameplay, resource eviction или GPU memory manager. Такие политики принадлежат Resources, Renderer и конкретным systems. Контракты allocator/tracker стабильны.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `allocation_tag.h`: `AllocationTag`.
- `allocator.h`: `IAllocator`, `DefaultAllocator`, `TrackingAllocator`, `IFrameAllocator`.
- `imemory_tracker.h`: `MemoryTagStatistics`, `IMemoryTracker`.
- `memory_tracker.h`: `MemoryTracker`.
