# Scene

## Purpose

Stores scene nodes, transforms, bounds and naive spatial queries without owning world or renderer behavior.

## Includes

- minimal math types `Vec3`, `Quat`, `Transform`
- `Aabb`, `SceneNodeId`, `SceneNodeState`
- node/transform/bounds registries
- spatial index and query contracts
- in-memory `SceneRuntime`

## Excludes

- world placement authority
- physics bodies
- renderer draw lists
- gameplay meaning of objects

## Public Contracts

- `transform.h`
- `bounds.h`
- `scene_node.h`
- `scene_state.h`
- `transform_registry.h`
- `scene_node_registry.h`
- `spatial_index.h`
- `scene_query.h`

## Forbidden Dependencies

No dependency on `World`, `Renderer` or `Physics`.

## States

`SceneNodeState` covers detached/attached, static/dynamic, dirty states and visibility candidate states.

## How To Use

Create nodes through the registry, set transforms and bounds, then query the current naive spatial index.

## Example

```cpp
auto node = scene.CreateNode();
scene.SetTransform(node.Value(), transform);
scene.SetBounds(node.Value(), bounds);
auto visible = scene.QueryAabb(camera_bounds);
```

## Testing Strategy

Validate node creation/destruction, transform/bounds dirty flags and AABB/sphere query results.
