#pragma once

namespace black {

struct ModelImporter;
struct StudioApp;
struct IAllocator;

ModelImporter* createFBXImporter(StudioApp& app, IAllocator& allocator);
void destroyFBXImporter(ModelImporter& importer);

} // namespace black