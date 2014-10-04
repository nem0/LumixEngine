#include <fbxsdk.h>
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		printf("usage: fbx_converter.exe source.fbx destination.msh");
		return 0;
	}
	FbxManager* sdk_manager = FbxManager::Create();
	FbxIOSettings *ios = FbxIOSettings::Create(sdk_manager, IOSROOT);
	sdk_manager->SetIOSettings(ios);
	FbxImporter* importer = FbxImporter::Create(sdk_manager, "");
	if (!importer->Initialize(argv[1], -1, sdk_manager->GetIOSettings()))
	{
		printf("Call to FbxImporter::Initialize() failed.\n");
		printf("Error returned: %s\n\n", importer->GetStatus().GetErrorString());
		exit(-1);
	}
	FbxScene* scene = FbxScene::Create(sdk_manager, "myScene");
	importer->Import(scene);
	importer->Destroy();

	FILE* fp;
	fopen_s(&fp, argv[2], "wb");
	if (!fp)
	{
		printf("Cound not open file \"%s\"", argv[2]);
		exit(-1);
	}
	char vertex_def[] = { 'p', 'n', 't' };
	FbxInt32 vertex_def_size = sizeof(vertex_def);
	fwrite(&vertex_def_size, sizeof(vertex_def_size), 1, fp);
	fwrite(vertex_def, 1, vertex_def_size, fp);

	FbxNode* lRootNode = scene->GetRootNode();
	FbxGeometryConverter converter(sdk_manager);
	converter.Triangulate(scene, true);
	std::vector<FbxMesh*> meshes;
	std::vector<FbxNode*> nodes;
	nodes.push_back(lRootNode);
	while (!nodes.empty())
	{
		auto node = nodes.back();
		nodes.pop_back();
		auto mesh = node->GetMesh();
		if (mesh)
		{
			meshes.push_back(mesh);
		}
		for (int i = 0; i < node->GetChildCount(); i++)
		{
			auto child = node->GetChild(i);
			nodes.push_back(child);
		}
	}

	FbxInt32 indices_count = 0;
	FbxInt32 vertices_count = 0;
	std::vector<int> index_offsets;
	std::vector<int> vertex_offsets;
	for (auto mesh : meshes)
	{
		mesh->SplitPoints();
		index_offsets.push_back(indices_count);
		vertex_offsets.push_back(vertices_count);
		indices_count += mesh->GetPolygonCount() * 3;
		vertices_count += mesh->GetControlPointsCount();
	}

	fwrite(&indices_count, sizeof(indices_count), 1, fp);
	int mesh_idx = 0;
	for (auto mesh : meshes)
	{
		int vertex_offset = vertex_offsets[mesh_idx];
		for (int polygon_idx = 0; polygon_idx < mesh->GetPolygonCount(); ++polygon_idx)
		{
			for (int triangle_vertex_idx = 0; triangle_vertex_idx < 3; ++triangle_vertex_idx)
			{
				FbxInt32 control_point_idx = mesh->GetPolygonVertex(polygon_idx, triangle_vertex_idx) + vertex_offset;
				fwrite(&control_point_idx, sizeof(control_point_idx), 1, fp);
			}
		}
		++mesh_idx;
	}

	fwrite(&vertices_count, sizeof(vertices_count), 1, fp);
	
	for (auto mesh : meshes)
	{
		auto uvs = mesh->GetLayer(0)->GetUVs();
		auto normals = mesh->GetLayer(0)->GetNormals();
		for (int i = 0, c = mesh->GetControlPointsCount(); i < c; ++i)
		{
			FbxVector4 vertex = mesh->GetControlPointAt(i);
			float coord[3] = { (float)vertex.mData[0], (float)vertex.mData[1], (float)vertex.mData[2] };
			fwrite(coord, sizeof(coord), 1, fp);
			auto normal = normals->GetDirectArray().GetAt(i);
			float float_normal[3] = { (float)normal.mData[0], (float)normal.mData[1], (float)normal.mData[2] };
			fwrite(float_normal, sizeof(float_normal), 1, fp);
			auto uv = uvs->GetDirectArray().GetAt(i);
			float float_uv[2] = { (float)uv.mData[0], (float)uv.mData[1] };
			fwrite(float_uv, sizeof(float_uv), 1, fp);
		}
	}

	FbxInt32 bone_count = 0;
	fwrite(&bone_count, sizeof(bone_count), 1, fp);
	FbxInt32 mesh_count = meshes.size();
	fwrite(&mesh_count, sizeof(mesh_count), 1, fp);
	for (auto mesh : meshes)
	{
		auto material = mesh->GetNode()->GetMaterial(0);
		auto material_name = material->GetName();
		FbxInt32 length = strlen(material_name);
		fwrite(&length, sizeof(length), 1, fp);
		fwrite(material_name, length, 1, fp);
			
		FbxInt32 mesh_tri_count = mesh->GetPolygonCount();
		fwrite(&mesh_tri_count, sizeof(mesh_tri_count), 1, fp);

		auto mesh_name = mesh->GetName();
		length = strlen(mesh_name);
		fwrite(&length, sizeof(length), 1, fp);
		fwrite(mesh_name, length, 1, fp);
	}
	
	sdk_manager->Destroy();
	fclose(fp);
	return 0;
}