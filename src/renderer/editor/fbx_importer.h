#pragma once

namespace Lumix {

struct ModelImporter;
struct StudioApp;
struct IAllocator;

ModelImporter* createFBXImporter(StudioApp& app, IAllocator& allocator);
void destroyFBXImporter(ModelImporter& importer);

} // namespace Lumix