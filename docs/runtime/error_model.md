# Runtime Error Model

Expected failures use stable error codes in `major.reason` form.

Examples:

- `time.invalid_scale`
- `world.object_not_found`
- `world.invalid_placement`
- `streaming.invalid_chunk`
- `physics.invalid_step`
- `navigation.query_not_found`
- `animation.clip_not_found`
- `audio.listener_not_found`
- `simulation.invalid_effect_target`
- `runtime_support.missing_dependency`

Human-readable messages may change. Tests and callers should branch on error codes, not message text.
