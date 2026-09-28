# Memory

## Назначение

Memory задает базовый контракт выделения памяти, tag based accounting и soft budgets. Модуль не заменяет стандартные контейнеры собственными и не является владельцем gameplay или resource memory policy.

## Публичная модель

`IAllocator` описывает выделение и освобождение блока с размером, alignment и `AllocationTag`. Нулевой размер является допустимым запросом и материализуется как один байт. Размер больше `PTRDIFF_MAX` считается недопустимым размером одного объекта. Alignment должен быть ненулевой степенью двойки. `Allocate` сообщает invalid size/alignment через `std::length_error`/`std::invalid_argument`, а operational OOM может распространяться как `std::bad_alloc`. `Deallocate` для `nullptr` или заведомо invalid size/alignment является controlled no-op, чтобы не вызывать undefined behavior в aligned delete.

`DefaultAllocator` использует aligned global new/delete. `TrackingAllocator` вызывает upstream allocator ровно один раз и публикует accounting только после ненулевого успешного результата. Exception или `nullptr` от upstream не меняют tracker state. Для корректного освобождения вызывающий код обязан передать те же `bytes`, `alignment` и tag, что использовались при allocation.

`MemoryTracker` хранит current usage, historical peak, число успешных allocation records и optional budget отдельно для каждого нормализованного tag. Byte counters и allocation count saturate at `SIZE_MAX`, не wrap around. `RecordFree` защищен от unsigned underflow и clamps usage к нулю. `ResetStatistics()` сохраняет текущие live bytes и budgets, сбрасывает historical allocation count и начинает новый peak с текущего usage.

`IFrameAllocator` пока является только extension point. Concrete production frame allocator в EngineBase отсутствует. Для будущей реализации `Reset()` является единственной явной lifetime boundary: storage предыдущего frame/batch может инвалидироваться при `Reset()`, но не неявно при обычном `Allocate`/`Deallocate`. При появлении production implementation этот контракт должен получить отдельный lifetime audit.

| Контракт | Роль |
|---|---|
| `AllocationTag` | категория памяти для учета |
| `IAllocator` | базовый allocate/deallocate port |
| `DefaultAllocator` | aligned heap implementation |
| `TrackingAllocator` | decorator, публикующий только успешные операции |
| `IFrameAllocator` | extension point с явной reset lifetime boundary |
| `IMemoryTracker` | thread safe accounting, budgets и statistics |
| `MemoryTagStatistics` | detached diagnostic snapshot |

## Владение, потоковость и ошибки

`MemoryTracker` является authoritative owner только своей диагностической статистики, budget values и tracking enabled state. Доступ к ним сериализован внутренним mutex. `DefaultAllocator` не хранит mutable owner state. `TrackingAllocator` хранит только ссылки на tracker и upstream, поэтому его concurrent use допустим только настолько, насколько concurrent use разрешен upstream allocator; сам accounting вызов остается thread safe через `IMemoryTracker` implementation.

Tracking enable/disable влияет только на последующие `RecordAllocate`/`RecordFree`. Переключение режима не выполняет ретроактивную реконструкцию allocation history. Если вызывающему коду требуется точный live usage, он не должен выключать tracking между парными record operations.

Invalid `AllocationTag` нормализуется в `Unknown`, поэтому не может выйти за границы массивов статистики. Budgets являются soft limits: `IsOverBudget()` становится true только при `usage > budget`, на точной границе возвращается false.

## Persistence и lifecycle

Memory accounting не является persistent engine state и не имеет snapshot/restore contract. У `MemoryTracker` нет lifecycle state machine или shutdown phase. `IFrameAllocator::Reset()` является интерфейсным lifetime hook, а не реализованным production lifecycle owner.

## Интеграция

`RegisterEngineBase()` регистрирует `IMemoryTracker` как service. Upper layers могут читать статистику и устанавливать budgets, но не должны зависеть от concrete `MemoryTracker`.

## Ограничения и стабильность

Модуль не реализует garbage collector, gameplay object pool, resource eviction или GPU memory manager. Allocator metadata остается caller supplied, поэтому mismatch между исходными и переданными в `Deallocate` допустимыми `bytes`/tag не может быть обнаружен без per-allocation ownership table и является нарушением precondition вызывающей стороны.

## Карта публичных заголовков

- `allocation_tag.h`: `AllocationTag`.
- `allocator.h`: `IAllocator`, `DefaultAllocator`, `TrackingAllocator`, `IFrameAllocator`.
- `imemory_tracker.h`: `MemoryTagStatistics`, `IMemoryTracker`.
- `memory_tracker.h`: `MemoryTracker`.
