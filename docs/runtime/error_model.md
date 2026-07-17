# Runtime Error Model

Expected failures use stable error codes in `major.reason` form.

Examples:

- `time.invalid_scale`
- `world.object_not_found`
- `world.invalid_placement`
- `world.invalid_reality_residency`
- `world.containment_cycle`
- `world.demotion_not_confirmed`
- `streaming.invalid_chunk`
- `physics.invalid_step`
- `physics.invalid_handle`
- `physics.unknown_handle`
- `physics.stale_handle`
- `physics.backend_failed`
- `navigation.query_not_found`
- `animation.clip_not_found`
- `audio.listener_not_found`
- `simulation.invalid_effect_target`
- `resource.invalid_handle`
- `resource.stale_handle`
- `resource.reference_underflow`
- `resource.memory_budget_exceeded`
- `resource.dependency_cycle`
- `persistence.conflict`
- `persistence.invalid_snapshot`
- `persistence.save_failed`
- `persistence.flush_failed`
- `runtime_support.missing_dependency`

Human-readable messages may change. Tests and callers should branch on error codes, not message text.

Codes are stable once they are part of a public runtime contract. New failures should add a new code rather than repurpose an existing one, and removals or semantic changes require migration notes in `api_stability.md`.
