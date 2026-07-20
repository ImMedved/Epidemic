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

Input не опрашивает gameplay systems и не вызывает их callbacks. Snapshot остается валиден до следующей публикации. Public raw-input contract заморожен.

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
