#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include <vector>

namespace epidemic::runtime
{
struct ResourceLoadArtifact
{
    ResourceId resource_id{};
    ResourceType type{};
    ResourcePayloadPtr payload{};
    std::vector<ResourceDependency> dependencies;
};

class IResourceLoader
{
  public:
    virtual ~IResourceLoader() = default;

    [[nodiscard]] virtual ResourceType GetResourceType() const = 0;
    [[nodiscard]] virtual foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) = 0;
};
} // namespace epidemic::runtime
