# Чек-лист inline API documentation

CLion показывает `///` и Doxygen-комментарии из public headers через Quick Documentation. Этот файл перечисляет заголовки, которые должны пройти отдельный comment-pass. Комментарий нужен перед каждым public interface, service bundle, factory, command, state enum и методом с нетривиальным ownership/lifecycle.

Тривиальные value getters можно описывать одной строкой. Для операций изменения состояния обязательны: назначение, параметры, результат, ownership, thread/lifecycle constraints и возможные error categories.

## EngineBase

- [ ] `Core/include/Epidemic/Core/application.h`
- [ ] `Core/include/Epidemic/Core/basic_configuration.h`
- [ ] `Core/include/Epidemic/Core/configuration.h`
- [ ] `Core/include/Epidemic/Core/event_bus.h`
- [ ] `Core/include/Epidemic/Core/frame_context.h`
- [ ] `Core/include/Epidemic/Core/frame_phase.h`
- [ ] `Core/include/Epidemic/Core/imodule.h`
- [ ] `Core/include/Epidemic/Core/main_thread_dispatcher.h`
- [ ] `Core/include/Epidemic/Core/module_manifest.h`
- [ ] `Core/include/Epidemic/Core/module_registry.h`
- [ ] `Core/include/Epidemic/Core/service_container.h`
- [ ] `Core/include/Epidemic/Core/task_scheduler.h`
- [ ] `Diagnostics/include/Epidemic/Diagnostics/console_logger.h`
- [ ] `Diagnostics/include/Epidemic/Diagnostics/counters.h`
- [ ] `Diagnostics/include/Epidemic/Diagnostics/logger.h`
- [ ] `Diagnostics/include/Epidemic/Diagnostics/profiling.h`
- [ ] `Diagnostics/include/Epidemic/Diagnostics/thread_context.h`
- [ ] `Foundation/include/Epidemic/Foundation/error.h`
- [ ] `Foundation/include/Epidemic/Foundation/handle.h`
- [ ] `Foundation/include/Epidemic/Foundation/path.h`
- [ ] `Foundation/include/Epidemic/Foundation/result.h`
- [ ] `Foundation/include/Epidemic/Foundation/string_id.h`
- [ ] `Foundation/include/Epidemic/Foundation/time.h`
- [ ] `Input/include/Epidemic/Input/iinput_system.h`
- [ ] `Input/include/Epidemic/Input/input_event.h`
- [ ] `Input/include/Epidemic/Input/input_snapshot.h`
- [ ] `Input/include/Epidemic/Input/input_system.h`
- [ ] `Input/include/Epidemic/Input/key_code.h`
- [ ] `Input/include/Epidemic/Input/keyboard_state.h`
- [ ] `Input/include/Epidemic/Input/mouse_button.h`
- [ ] `Input/include/Epidemic/Input/mouse_state.h`
- [ ] `Memory/include/Epidemic/Memory/allocation_tag.h`
- [ ] `Memory/include/Epidemic/Memory/allocator.h`
- [ ] `Memory/include/Epidemic/Memory/imemory_tracker.h`
- [ ] `Memory/include/Epidemic/Memory/memory_tracker.h`
- [ ] `Platform/include/Epidemic/Platform/idynamic_library.h`
- [ ] `Platform/include/Epidemic/Platform/iplatform_runtime.h`
- [ ] `Platform/include/Epidemic/Platform/iwindow.h`
- [ ] `Platform/include/Epidemic/Platform/iwindow_system.h`
- [ ] `Platform/include/Epidemic/Platform/native_window_handle.h`
- [ ] `Platform/include/Epidemic/Platform/platform_event.h`
- [ ] `Platform/include/Epidemic/Platform/windows_platform_runtime.h`
- [ ] `RHI/include/Epidemic/RHI/descriptors.h`
- [ ] `RHI/include/Epidemic/RHI/irhi_command_context.h`
- [ ] `RHI/include/Epidemic/RHI/irhi_device.h`
- [ ] `RHI/include/Epidemic/RHI/irhi_swap_chain.h`
- [ ] `RHI/include/Epidemic/RHI/null_rhi_device.h`
- [ ] `RHI/include/Epidemic/RHI/pixel_format.h`
- [ ] `RHI/include/Epidemic/RHI/presentation_surface_handle.h`
- [ ] `RHI_D3D11/include/Epidemic/RHI_D3D11/d3d11_rhi_device.h`
- [ ] `Support/include/Epidemic/EngineBase/engine_base_support.h`
- [ ] `Support/include/Epidemic/EngineBase/graphics_backend.h`

## EngineRuntime

- [ ] `Animation/include/Epidemic/Runtime/Animation/animation.h`
- [ ] `Animation/include/Epidemic/Runtime/Animation/animation_runtime.h`
- [ ] `Animation/include/Epidemic/Runtime/Animation/animation_types.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_catalog.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_catalog_writer.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_dependency_manifest.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_location.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_location_resolver.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_metadata.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_services.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_state.h`
- [ ] `Assets/include/Epidemic/Runtime/Assets/asset_type.h`
- [ ] `Audio/include/Epidemic/Runtime/Audio/audio.h`
- [ ] `Audio/include/Epidemic/Runtime/Audio/audio_runtime.h`
- [ ] `Audio/include/Epidemic/Runtime/Audio/audio_types.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/climate_profile.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/environment_projection.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/environment_runtime.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/environment_services.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/environment_snapshot.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/environment_update.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/season_state.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/surface_state.h`
- [ ] `Environment/include/Epidemic/Runtime/Environment/weather_state.h`
- [ ] `Navigation/include/Epidemic/Runtime/Navigation/navigation.h`
- [ ] `Navigation/include/Epidemic/Runtime/Navigation/navigation_runtime.h`
- [ ] `Navigation/include/Epidemic/Runtime/Navigation/navigation_types.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/dirty_tracker.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/lazy_rule_record.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_backend.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_location.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_operation.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_payload.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_policy.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_services.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_state.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistence_store.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistent_object_store.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/persistent_record.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/save_transaction.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/tombstone_store.h`
- [ ] `Persistence/include/Epidemic/Runtime/Persistence/zone_override_store.h`
- [ ] `Physics/include/Epidemic/Runtime/Physics/physics_event_buffer.h`
- [ ] `Physics/include/Epidemic/Runtime/Physics/physics_query.h`
- [ ] `Physics/include/Epidemic/Runtime/Physics/physics_scene.h`
- [ ] `Physics/include/Epidemic/Runtime/Physics/physics_types.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/render_resource_bridge.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/render_scene.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/render_types.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/renderer_runtime.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/renderer_services.h`
- [ ] `Renderer/include/Epidemic/Runtime/Renderer/view_system.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_dependency_graph.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_handle.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_loader.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_loader_registry.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_manager.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_payload.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_request.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_result.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_services.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_state.h`
- [ ] `Resources/include/Epidemic/Runtime/Resources/resource_type.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_budget.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_foundation.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_handles.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_ids.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_operation.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_states.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/runtime_time.h`
- [ ] `RuntimeFoundation/include/Epidemic/Runtime/Foundation/spatial.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/bounds.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/scene_node.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/scene_node_registry.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/scene_query.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/scene_services.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/scene_state.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/spatial_index.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/transform.h`
- [ ] `Scene/include/Epidemic/Runtime/Scene/transform_registry.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/archive_reader.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/archive_value.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/archive_writer.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/migration.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/migration_executor.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/migration_registry.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/schema_version.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/serialization_error.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/serialization_services.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/serializer.h`
- [ ] `Serialization/include/Epidemic/Runtime/Serialization/serializer_registry.h`
- [ ] `Simulation/include/Epidemic/Runtime/Simulation/simulation.h`
- [ ] `Simulation/include/Epidemic/Runtime/Simulation/simulation_runtime.h`
- [ ] `Simulation/include/Epidemic/Runtime/Simulation/simulation_types.h`
- [ ] `Streaming/include/Epidemic/Runtime/Streaming/residency_controller.h`
- [ ] `Streaming/include/Epidemic/Runtime/Streaming/streaming_priority_resolver.h`
- [ ] `Streaming/include/Epidemic/Runtime/Streaming/streaming_runtime.h`
- [ ] `Streaming/include/Epidemic/Runtime/Streaming/streaming_sources.h`
- [ ] `Streaming/include/Epidemic/Runtime/Streaming/streaming_types.h`
- [ ] `Time/include/Epidemic/Runtime/Time/game_calendar.h`
- [ ] `Time/include/Epidemic/Runtime/Time/game_time.h`
- [ ] `Time/include/Epidemic/Runtime/Time/time_events.h`
- [ ] `Time/include/Epidemic/Runtime/Time/time_runtime.h`
- [ ] `Time/include/Epidemic/Runtime/Time/time_scale.h`
- [ ] `Time/include/Epidemic/Runtime/Time/time_snapshot.h`
- [ ] `Time/include/Epidemic/Runtime/Time/time_state.h`
- [ ] `World/include/Epidemic/Runtime/World/chunk.h`
- [ ] `World/include/Epidemic/Runtime/World/object_materialization.h`
- [ ] `World/include/Epidemic/Runtime/World/object_placement.h`
- [ ] `World/include/Epidemic/Runtime/World/region.h`
- [ ] `World/include/Epidemic/Runtime/World/world_commands.h`
- [ ] `World/include/Epidemic/Runtime/World/world_invariants.h`
- [ ] `World/include/Epidemic/Runtime/World/world_location.h`
- [ ] `World/include/Epidemic/Runtime/World/world_object.h`
- [ ] `World/include/Epidemic/Runtime/World/world_object_registry.h`
- [ ] `World/include/Epidemic/Runtime/World/world_query.h`
- [ ] `World/include/Epidemic/Runtime/World/world_services.h`
- [ ] `World/include/Epidemic/Runtime/World/world_state.h`

## Runtime Support

Support не комментируется окончательно до заморозки composition API. После завершения нужно пройти:
- [ ] `Support/include/Epidemic/Runtime/Support/runtime_support.h`

## Минимальный шаблон

```cpp
/// Выполняет одну законченную операцию контракта.
///
/// @param value Входное значение и требования к его lifetime.
/// @return Результат операции или стабильная ошибка.
/// @note Метод вызывается только на runtime thread.
[[nodiscard]] virtual foundation::Result<void>
Execute(const Value& value) = 0;
```
