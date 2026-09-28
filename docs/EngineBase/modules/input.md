# Input

## Назначение

Input превращает поток platform events в согласованный immutable snapshot одного кадра. Он отделяет физический ввод от gameplay meaning.

## Модель

`IInputSystem` принимает одиночные или пакетные `PlatformEvent`, затем `PublishSnapshot()` формирует `InputSnapshot`. Snapshot содержит `KeyboardState`, `MouseState` и sequence событий. Текущее состояние отвечает на вопрос, удерживается ли клавиша сейчас, а events описывают переходы внутри кадра.

`KeyCode` и `MouseButton` являются platform-neutral enums. `InputEvent` не содержит действий игрока, контекста UI или rebinding.

## Порядок использования

Platform events должны быть переданы до `PublishSnapshot()`. Все consumers одного кадра читают один и тот же snapshot. `Reset()` используется при потере контекста или тестовой переинициализации, а не в конце каждого обычного кадра.

Gameplay layer преобразует snapshot в собственный command set. Например, одна и та же клавиша может означать движение персонажа, управление кораблем или навигацию меню в зависимости от активного контекста.

## Инварианты и стабильность

Input не опрашивает gameplay systems и не вызывает их callbacks. Snapshot остается валиден до следующей публикации. События, поставленные в очередь после публикации, не меняют уже опубликованный snapshot и становятся видимыми только на следующем `PublishSnapshot()`.

Held state переносится между кадрами, transient press/release, mouse motion и wheel state очищаются в начале следующей публикации. Несколько переходов одной клавиши или кнопки в одном кадре сохраняются в `CurrentEvents()` в исходном порядке, при этом snapshot хранит итоговый held state и факт реально произошедших press/release transitions. Release для уже отпущенной клавиши или кнопки не создаёт ложный transition flag. Focus loss очищает held keyboard/mouse state и capture. `Reset()` очищает published state, очередь и events без синтетических gameplay transitions. Unknown и invalid key/button values игнорируются и не alias-ят валидный storage slot.

Public raw-input contract заморожен.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `iinput_system.h`: `IInputSystem`.
- `input_event.h`: `InputEventType`, `InputEvent`.
- `input_snapshot.h`: `InputSnapshot`.
- `input_system.h`: `InputSystem`.
- `key_code.h`: `KeyCode`.
- `keyboard_state.h`: `KeyboardState`.
- `mouse_button.h`: `MouseButton`.
- `mouse_state.h`: `MouseState`.
