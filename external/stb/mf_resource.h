// mf_resource is a platform-independent system for storing text and binary files in the application's executable
// It is similar to windows' or Qt's resource system.
// MIT License

// usage:
// create a directory with files you want to include in your executable
// create a compiler exe:
// int main()
// {
//     mf_compiler_dir("path/to/dir/", "*", "resources.cpp");
// }
// This will process path/to/dir/ directory and create resources.cpp.
// Now in your application you can get any resource
// const mf_resource* res = mf_get_resource("path/to/dir/test.txt");
// if(res) printf("%s", res->value);

// TODO:
// - optimize mf_get_resource - something better than linear search with strcmp
// - define which files to compile in yaml file
// - text mode
// - handle case when no resources are found

#pragma once
#ifdef _WIN32
	#ifndef _CRT_SECURE_NO_WARNINGS
		#define _CRT_SECURE_NO_WARNINGS
	#endif
	#ifndef MF_RESOURCE_DONT_INCLUDE_WINDOWS_H
		#include <windows.h>
	#endif
#elif defined(__linux__)
	#include <dirent.h>
	#include <linux/limits.h>
	#include <stdio.h>
	#include <string.h>
#else
	#error Unsupported platform
#endif


#include <cstdio>
#include <cstring>


struct mf_resource
{
	const char* path;
	const unsigned char* value;
	size_t size;
};


struct mf_resource_compiler
{
	FILE* fout;
	int counter;
};


extern const mf_resource* mf_resources;
extern int mf_resources_count;


inline mf_resource_compiler mf_begin_compile(const char* output)
{
	mf_resource_compiler compiler;
	compiler.fout = fopen(output, "wb");
	if (!compiler.fout) return compiler;

	compiler.counter = 0;
	fputs("#include \"mf_resource.h\"\n", compiler.fout);
	return compiler;
}


inline bool mf_compile_file(const char* file, FILE* fout, int* counter)
{
	FILE* fin = fopen(file, "rb");
	if (!fin) return false;
	size_t size = 0;
	fprintf(fout, "const char* mf_resource_%d_path = \"%s\";\n", *counter, file);
	fprintf(fout, "const unsigned char mf_resource_%d_value[] = {", *counter);
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
	fprintf(fout, "const size_t mf_resource_%d_size = %d;\n", *counter, (int)size);

	++(*counter);
	fclose(fin);
	return true;
}


#ifdef _WIN32
inline bool mf_compile_dir_internal(const char* path, const char* pattern, FILE* fout, int* counter)
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
			if (!mf_compile_dir_internal(tmp, pattern, fout, counter))
			{
				FindClose(h);
				return false;
			}
		}
		else
		{
			if (!mf_compile_file(tmp, fout, counter))
			{
				FindClose(h);
				return false;
			}
		}
	} while (FindNextFile(h, &data) != FALSE);
	FindClose(h);
	return true;
}
#elif defined(__linux__)
inline bool mf_compile_dir_internal(const char* path, const char* pattern, FILE* fout, int* counter)
{
	DIR* dir = opendir(path);
	if (!dir) return true;

	char tmp[PATH_MAX];
	struct dirent* dirent = readdir(dir);
	while (dirent != NULL)
	{
		if (strcmp(dirent->d_name, ".") == 0) continue;
		if (strcmp(dirent->d_name, "..") == 0) continue;

		strcpy(tmp, path);
		strcat(tmp, dirent->d_name);

		if (dirent->d_type == DT_DIR)
		{
			if (!mf_compile_dir_internal(tmp, pattern, fout, counter))
			{
				closedir(dir);
				return false;
			}
		}
		else
		{
			if (!mf_compile_file(tmp, fout, counter))
			{
				closedir(dir);
				return false;
			}
		}
		dirent = readdir(dir);
	}
	closedir(dir);
	return true;
}
#endif


inline bool mf_compile(mf_resource_compiler* compiler, const char* path, const char* pattern)
{
	return mf_compile_dir_internal(path, pattern, compiler->fout, &compiler->counter);
}


inline void mf_end_compile(mf_resource_compiler* compiler)
{
	fputs("const mf_resource mf_resources_storage[] = {\n", compiler->fout);
	for (int i = 0; i < compiler->counter; ++i)
	{
		fprintf(compiler->fout, "{ mf_resource_%d_path, mf_resource_%d_value, mf_resource_%d_size }, ", i, i, i);
	}
	fputs("};\nconst mf_resource* mf_resources = mf_resources_storage;\n", compiler->fout);
	fputs("int mf_resources_count = sizeof(mf_resources_storage) / sizeof(mf_resources_storage[0]);\n", compiler->fout);
	fclose(compiler->fout);
}


inline bool mf_compile_dir(const char* path, const char* pattern, const char* output)
{
	FILE* fout = fopen(output, "wb");
	if (!fout) return false;

	int counter = 0;
	fputs("#include \"mf_resource.h\"\n", fout);
	bool res = mf_compile_dir_internal(path, pattern, fout, &counter);
	fputs("const mf_resource mf_resources_storage[] = {\n", fout);
	for (int i = 0; i < counter; ++i)
	{
		fprintf(fout, "{ mf_resource_%d_path, mf_resource_%d_value, mf_resource_%d_size }, ", i, i, i);
	}
	fputs("};\nconst mf_resource* mf_resources = mf_resources_storage;\n", fout);
	fputs("int mf_resources_count = sizeof(mf_resources_storage) / sizeof(mf_resources_storage[0]);\n", fout);
	fclose(fout);
	return res;
}


inline const mf_resource* mf_get_resource(const char* path)
{
	for (int i = 0; i < mf_resources_count; ++i)
	{
		const mf_resource* res = &mf_resources[i];
		if (strcmp(path, res->path) == 0) return res;
	}
	return (const mf_resource*)0;
}


inline const mf_resource* mf_get_all_resources()
{
	return mf_resources;
}


inline int mf_get_all_resources_count()
{
	return mf_resources_count;
}