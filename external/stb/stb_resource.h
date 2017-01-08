// stb_resource is a platform-independent system for storing text and binary files in the application's executable
// It is similar to windows' of Qt's resource system.
// MIT License

// usage:
// create a directory with files you want to include in your executable
// create a compiler exe:
// int main()
// {
//     stb_compiler_dir("path/to/dir/", "*", "resources.cpp");
// }
// This will process path/to/dir/ directory and create resources.cpp.
// Now in your application you can get any resource 
// const stb_resource* res = stb_get_resource("path/to/dir/test.txt");
// if(res) printf("%s", res->value);

// TODO:
// - Linux version
// - optimize stb_get_resource - something better than linear search with strcmp
// - define which files to compile in yaml file
// - text mode

#pragma once
#ifndef _WIN32
#error unsupported platform
#else
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef STB_RESOURCE_DONT_INCLUDE_WINDOWS_H
#include <windows.h>
#endif
#endif

#include <cstdio>


struct stb_resource
{
	const char* path;
	const unsigned char* value;
	size_t size;
};


extern const stb_resource* stb_resources;
extern int stb_resources_count;


inline bool stb_compile_file(const char* file, FILE* fout, int* counter)
{
	FILE* fin = fopen(file, "rb");
	if (!fin) return false;
	size_t size = 0;
	fprintf(fout, "const char* stb_resource_%d_path = \"%s\";\n", *counter, file);
	fprintf(fout, "const unsigned char stb_resource_%d_value[] = {", *counter);
	while (!feof(fin))
	{
		unsigned char tmp[1024];
		size_t read = fread(tmp, 1, 1024, fin);
		for (size_t i = 0; i < read; ++i)
		{
			fprintf(fout, "0x%x,", (int)tmp[i]);
		}
		size += read;
	}
	fputs("};\n", fout);
	fprintf(fout, "const size_t stb_resource_%d_size = %d;\n", *counter, (int)size);

	++(*counter);
	fclose(fin);
	return true;
}


inline bool stb_compile_dir(const char* path, const char* pattern, FILE* fout, int* counter)
{
	WIN32_FIND_DATAA data;
	char tmp[MAX_PATH];
	strcpy_s(tmp, path);
	strcat_s(tmp, pattern);
	HANDLE h = FindFirstFile(tmp, &data);
	if (h == INVALID_HANDLE_VALUE) return true;
	do
	{
		if (strcmp(data.cFileName, ".") == 0) continue;
		if (strcmp(data.cFileName, "..") == 0) continue;
		bool is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		strcpy_s(tmp, path);
		strcat_s(tmp, data.cFileName);
		if (is_directory)
		{
			strcat_s(tmp, "/");
			if (!stb_compile_dir(tmp, pattern, fout, counter))
			{
				FindClose(h);
				return false;
			}
		}
		else
		{
			if (!stb_compile_file(tmp, fout, counter))
			{
				FindClose(h);
				return false;
			}
		}
	} while (FindNextFile(h, &data) != FALSE);
	FindClose(h);
	return true;
}


inline bool stb_compile_dir(const char* path, const char* pattern, const char* output)
{
	FILE* fout = fopen(output, "wb");
	if (!fout) return false;

	int counter = 0;
	fputs("#include \"stb_resource.h\"\n", fout);
	bool res = stb_compile_dir(path, pattern, fout, &counter);
	fputs("const stb_resource stb_resources_storage[] = {\n", fout);
	for (int i = 0; i < counter; ++i)
	{
		fprintf(fout, "{ stb_resource_%d_path, stb_resource_%d_value, stb_resource_%d_size }, ", i, i, i);
	}
	fputs("};\nconst stb_resource* stb_resources = stb_resources_storage;\n", fout);
	fputs("int stb_resources_count = sizeof(stb_resources_storage) / sizeof(stb_resources_storage[0]);\n", fout);
	fclose(fout);
	return res;
}


inline const stb_resource* stb_get_resource(const char* path)
{
	for (int i = 0; i < stb_resources_count; ++i)
	{
		const stb_resource* res = &stb_resources[i];
		if (strcmp(path, res->path) == 0) return res;
	}
	return (const stb_resource*)0;
}


inline const stb_resource* stb_get_all_resources()
{
	return stb_resources;
}


inline int stb_get_all_resources_count()
{
	return stb_resources_count;
}