# Audio

## Назначение

Audio управляет sound metadata, emitter/listener handles, backend voices, spatial state, mixer hierarchy, fades, one-shots и clip resources.

## Контракты

`ISoundRegistry` хранит `SoundDesc`. `IAudioRuntime` создает emitter, управляет Play/Pause/Resume/Stop/Fade/Virtualize, выполняет Tick и terminal shutdown. `IListenerSystem` управляет listeners. `IMixerSystem` хранит groups. `IAudioBackend` владеет voices и listener representation. `IAudioResourceSource` возвращает typed `IAudioClipResource`, который может содержать encoded bytes или `IAudioStreamSource`.

## Playback

Base gain, fade multiplier и mixer gain разделены. Pause сохраняет незавершенный fade state, Resume продолжает его. Zero-duration fade завершается синхронно. Virtualize освобождает backend voice; следующий Play начинает clip заново согласно baseline policy.

One-shot completion проверяется backend. Failed voice/listener cleanup сохраняет handles для retry и не повреждает survivor collections.

## Shutdown

После начала shutdown новая работа отклоняется, даже если cleanup временно завершился ошибкой. Повторный shutdown продолжает cleanup; успешный повторный вызов идемпотентен. ID/generation allocators защищены от overflow.

## Стабильность

После финальных lifecycle fixes модуль frozen. XAudio2/FMOD и декодеры подключаются через существующие ports.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `audio.h`: factory-функции или backend implementation без отдельного публичного типа.
- `audio_runtime.h`: `ISoundRegistry`, `IAudioBackend`, `IAudioResourceSource`, `IAudioTransformSource`, `IAudioRuntime`, `IListenerSystem`, `IAudioEventQueue`, `IMixerSystem`, `AudioDependencies`, `AudioServices`.
- `audio_types.h`: `SoundId`, `AudioEmitterId`, `AudioListenerId`, `BackendVoiceHandle`, `MixerGroupId`, `AudioEmitterHandle`, `AudioListenerHandle`, `AudioEventSpace`, `AudioEventOverflowPolicy`, `SoundState`, `EmitterState`, `MixerFadeState`, `SoundDesc`, `AudioClipPayload`, `IAudioClipResource`, `AudioClipStorage`, `AudioClipFormat`, `IAudioStreamSource`, `AudioStreamReadResult`, `AudioBackendOptions`, `AudioSpatialState`, `AudioEmitterDesc`, `AudioVoiceDesc`, `AudioEmitterSnapshot`, `AudioListenerDesc`, `AudioEvent`, `MixerGroupState`, `AudioOptions`.
