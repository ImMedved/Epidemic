# Audio

## Назначение

Audio управляет sound metadata, emitter/listener handles, backend voices, spatial state, mixer hierarchy, fades, one-shots и clip resources.

## Контракты

`ISoundRegistry` хранит `SoundDesc`. `IAudioRuntime` создает emitter, управляет Play/Pause/Resume/Stop/Fade/Virtualize, выполняет Tick и terminal shutdown. `IListenerSystem` управляет listeners. `IMixerSystem` хранит groups. `IAudioBackend` владеет voices и listener representation. `IAudioResourceSource` возвращает typed `IAudioClipResource`, который может содержать encoded bytes или `IAudioStreamSource`.

Backend boundary имеет freeze-level retry contract. Повтор той же setter/control projection безопасен. Failure/exception setter/control command означает, что exact retry безопасен. Failure/exception `Update(delta)` означает, что `delta` не был принят и temporal state backend не продвинулся. `CreateVoice`/`CreateBackendListener` передают ownership только при success; failed destroy сохраняет ownership для retry.

`AudioClipPayload`, полученный от resource source, обязан относиться к запрошенному `SoundId`. Failure/exception resource или transform source сохраняется как controlled Runtime error и не приводит к backend acquisition.

## Playback

Base gain, fade multiplier и mixer gain разделены. Pause сохраняет незавершенный fade state, Resume продолжает его. Zero-duration fade завершается синхронно. Virtualize освобождает backend voice; следующий Play начинает clip заново согласно baseline policy.

До любого внешнего `CreateVoice` Runtime заранее резервирует cleanup ownership. Для batch one-shot Tick capacity резервируется на весь batch до первого acquisition. Поэтому failed rollback после successful `CreateVoice` может сохранить `BackendVoiceHandle` и pinned clip resource без новой allocation. One-shot completion проверяется backend. Failed voice/listener cleanup сохраняет handles для retry и не повреждает survivor collections.

## Shutdown

После начала shutdown новая работа отклоняется, даже если cleanup временно завершился ошибкой. Cleanup voice/listener ownership выполняется best-effort: ошибка одного объекта не блокирует независимые cleanup steps. Повторный shutdown продолжает только незавершенный cleanup; успешный повторный вызов идемпотентен. ID/generation allocators защищены от overflow.

## Статус

После локального аудита Goal 3 и canonical evidence convergence модуль имеет статус `LOCAL_READY`. Это не означает `FROZEN`: системные persistence/determinism, integration, fault/load qualification и окончательный freeze выполняются в последующих Goals 5–9.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `audio.h`: factory-функции или backend implementation без отдельного публичного типа.
- `audio_runtime.h`: `ISoundRegistry`, `IAudioBackend`, `IAudioResourceSource`, `IAudioTransformSource`, `IAudioRuntime`, `IListenerSystem`, `IAudioEventQueue`, `IMixerSystem`, `AudioDependencies`, `AudioServices`.
- `audio_types.h`: `SoundId`, `AudioEmitterId`, `AudioListenerId`, `BackendVoiceHandle`, `MixerGroupId`, `AudioEmitterHandle`, `AudioListenerHandle`, `AudioEventSpace`, `AudioEventOverflowPolicy`, `SoundState`, `EmitterState`, `MixerFadeState`, `SoundDesc`, `AudioClipPayload`, `IAudioClipResource`, `AudioClipStorage`, `AudioClipFormat`, `IAudioStreamSource`, `AudioStreamReadResult`, `AudioBackendOptions`, `AudioSpatialState`, `AudioEmitterDesc`, `AudioVoiceDesc`, `AudioEmitterSnapshot`, `AudioListenerDesc`, `AudioEvent`, `MixerGroupState`, `AudioOptions`.
