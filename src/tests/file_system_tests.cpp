#include "core/array.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/stream.h"
#include "engine/file_system.h"
#include "engine/resource.h"
#include "tests/common.h"

using namespace Lumix;

namespace {

static const char* TEST_FILE = "file_system_stream_test.bin";
static constexpr u32 TEST_FILE_SIZE = 4096;

static u8 expectedByte(u32 i) { return (u8)(i * 7 + 3); }

struct Fixture {
	Fixture() : fs(FileSystem::create(".", getGlobalAllocator())) {
		fs->mount(".", "");
		u8 data[TEST_FILE_SIZE];
		for (u32 i = 0; i < TEST_FILE_SIZE; ++i) data[i] = expectedByte(i);
		created = fs->saveContentSync(Path(TEST_FILE), Span<const u8>(data, TEST_FILE_SIZE));
	}

	~Fixture() {
		// let the worker thread finish everything before the file is deleted
		waitIdle();
		fs->deleteFile(TEST_FILE);
	}

	// pump callbacks until all queued work is done, false on timeout
	bool waitIdle() {
		os::Timer timer;
		while (fs->hasWork()) {
			fs->processCallbacks();
			if (timer.getTimeSinceStart() > 5.f) return false;
			os::sleep(1);
		}
		return true;
	}

	UniquePtr<FileSystem> fs;
	bool created = false;
};

struct StreamResult {
	void onStream(u64 ud, Span<const u8> content, bool ok) {
		++call_count;
		success = ok;
		user_data = ud;
		data.clear();
		for (u8 b : content) data.push(b);
	}

	u32 call_count = 0;
	bool success = false;
	u64 user_data = 0;
	Array<u8> data = Array<u8>(getGlobalAllocator());
};

struct ContentResult {
	void onContent(Span<const u8> content, bool ok) {
		++call_count;
		success = ok;
		size = (u32)content.length();
	}

	u32 call_count = 0;
	bool success = false;
	u32 size = 0;
};

bool testFileSystemGetContentAsync() {
	Fixture f;
	ASSERT_TRUE(f.created);
	ContentResult r;
	FileSystem::ContentCallback cb;
	cb.bind<&ContentResult::onContent>(&r);
	f.fs->getContent(Path(TEST_FILE), cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(r.success);
	ASSERT_EQ(TEST_FILE_SIZE, r.size);
	return true;
}

bool testFileSystemStreamRead() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path(TEST_FILE));
	ASSERT_TRUE((u32)h != 0);

	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 42, 100, 64, cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(r.success);
	ASSERT_EQ(42u, (u32)r.user_data);
	ASSERT_EQ(64u, (u32)r.data.size());
	for (u32 i = 0; i < 64; ++i) ASSERT_EQ((u32)expectedByte(100 + i), (u32)r.data[i]);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemStreamMultipleReads() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path(TEST_FILE));

	StreamResult a, b, c;
	FileSystem::StreamCallback cb_a, cb_b, cb_c;
	cb_a.bind<&StreamResult::onStream>(&a);
	cb_b.bind<&StreamResult::onStream>(&b);
	cb_c.bind<&StreamResult::onStream>(&c);
	// out-of-order offsets, each request must get its own data and user_data
	f.fs->stream(h, 1, 2000, 16, cb_a);
	f.fs->stream(h, 2, 0, 32, cb_b);
	f.fs->stream(h, 3, 4000, 96, cb_c);
	ASSERT_TRUE(f.waitIdle());

	ASSERT_EQ(1u, a.call_count);
	ASSERT_EQ(1u, b.call_count);
	ASSERT_EQ(1u, c.call_count);
	ASSERT_EQ(1u, (u32)a.user_data);
	ASSERT_EQ(2u, (u32)b.user_data);
	ASSERT_EQ(3u, (u32)c.user_data);
	ASSERT_TRUE(a.success && b.success && c.success);
	ASSERT_EQ(16u, (u32)a.data.size());
	ASSERT_EQ(32u, (u32)b.data.size());
	ASSERT_EQ(96u, (u32)c.data.size());
	for (u32 i = 0; i < 16; ++i) ASSERT_EQ((u32)expectedByte(2000 + i), (u32)a.data[i]);
	for (u32 i = 0; i < 32; ++i) ASSERT_EQ((u32)expectedByte(i), (u32)b.data[i]);
	for (u32 i = 0; i < 96; ++i) ASSERT_EQ((u32)expectedByte(4000 + i), (u32)c.data[i]);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemStreamPastEof() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path(TEST_FILE));

	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 7, TEST_FILE_SIZE - 8, 64, cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	ASSERT_EQ(7u, (u32)r.user_data);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemStreamOpenMissingFile() {
	Fixture f;
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("this_file_does_not_exist.bin"));

	// reading from a stream whose open failed must report failure, not crash
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 5, 0, 16, cb);
	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	ASSERT_EQ(5u, (u32)r.user_data);
	return true;
}

bool testFileSystemStreamUsesMounts() {
	Fixture f;
	ASSERT_TRUE(f.created);
	// the fixture's "" mount matches everything first, so use the built-in "engine" mount (also ".")
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("engine/", TEST_FILE));

	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 1, 10, 16, cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(r.success);
	for (u32 i = 0; i < 16; ++i) ASSERT_EQ((u32)expectedByte(10 + i), (u32)r.data[i]);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemStreamHandlesUnique() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle a = f.fs->openStreaming(Path(TEST_FILE));
	const FileSystem::StreamHandle b = f.fs->openStreaming(Path(TEST_FILE));
	ASSERT_TRUE((u32)a != 0);
	ASSERT_TRUE((u32)b != 0);
	ASSERT_TRUE(a != b);

	f.fs->close(a);
	f.fs->close(b);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemStreamAfterClose() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path(TEST_FILE));
	f.fs->close(h);

	// stream on a closed handle must fail gracefully
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 9, 0, 16, cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	return true;
}

bool testFileSystemStreamMixedWithGetContent() {
	Fixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path(TEST_FILE));

	StreamResult sr;
	FileSystem::StreamCallback scb;
	scb.bind<&StreamResult::onStream>(&sr);
	ContentResult cr;
	FileSystem::ContentCallback ccb;
	ccb.bind<&ContentResult::onContent>(&cr);

	f.fs->stream(h, 1, 0, 8, scb);
	f.fs->getContent(Path(TEST_FILE), ccb);
	f.fs->stream(h, 2, 8, 8, scb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, cr.call_count);
	ASSERT_TRUE(cr.success);
	ASSERT_EQ(2u, sr.call_count);
	ASSERT_TRUE(sr.success);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

// builds a pak with the layout PackFileSystem expects: count, {hash, offset, size}[], data
struct PakFixture {
	PakFixture() : loose(FileSystem::create(".", getGlobalAllocator())) {
		loose->mount(".", "");
		// the last two are compiled resources, the first has a plain header and the second a COMPRESSED one
		static const char* names[4] = { "pak_a.bin", "pak_b.bin", "pak_raw.res", "pak_packed.res" };
		static const u32 sizes[4] = { 300, 500, 200, 200 };
		OutputMemoryStream header(getGlobalAllocator());
		OutputMemoryStream body(getGlobalAllocator());
		header.write((u32)4);
		for (u32 e = 0; e < 4; ++e) {
			const bool is_res = e >= 2;
			header.write(Path(names[e]).getHash());
			header.write((u64)body.size());
			header.write((u64)(sizes[e] + (is_res ? sizeof(CompiledResourceHeader) : 0)));
			if (is_res) {
				CompiledResourceHeader res_header;
				if (e == 3) res_header.flags |= CompiledResourceHeader::COMPRESSED;
				body.write(&res_header, sizeof(res_header));
			}
			for (u32 i = 0; i < sizes[e]; ++i) body.write((u8)(i * (e + 3) + e));
		}
		header.write(body.data(), body.size());
		created = loose->saveContentSync(Path(PAK_FILE), Span<const u8>((const u8*)header.data(), (u32)header.size()));
		if (created) fs = FileSystem::createPacked(PAK_FILE, getGlobalAllocator());
	}

	~PakFixture() {
		fs.reset();
		loose->deleteFile(PAK_FILE);
	}

	bool waitIdle() {
		os::Timer timer;
		while (fs->hasWork()) {
			fs->processCallbacks();
			if (timer.getTimeSinceStart() > 5.f) return false;
			os::sleep(1);
		}
		return true;
	}

	static constexpr const char* PAK_FILE = "file_system_test.pak";
	UniquePtr<FileSystem> loose;
	UniquePtr<FileSystem> fs;
	bool created = false;
};

bool testFileSystemPakStreamRead() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle a = f.fs->openStreaming(Path("pak_a.bin"));
	const FileSystem::StreamHandle b = f.fs->openStreaming(Path("pak_b.bin"));

	StreamResult ra, rb;
	FileSystem::StreamCallback cba, cbb;
	cba.bind<&StreamResult::onStream>(&ra);
	cbb.bind<&StreamResult::onStream>(&rb);
	// interleaved reads of two entries
	f.fs->stream(b, 2, 100, 40, cbb);
	f.fs->stream(a, 1, 10, 20, cba);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_TRUE(ra.success && rb.success);
	ASSERT_EQ(20u, (u32)ra.data.size());
	ASSERT_EQ(40u, (u32)rb.data.size());
	for (u32 i = 0; i < 20; ++i) ASSERT_EQ((u32)(u8)((10 + i) * 3), (u32)ra.data[i]);
	for (u32 i = 0; i < 40; ++i) ASSERT_EQ((u32)(u8)((100 + i) * 4 + 1), (u32)rb.data[i]);

	f.fs->close(a);
	f.fs->close(b);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemPakStreamBounds() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle a = f.fs->openStreaming(Path("pak_a.bin"));

	StreamResult inside, past, huge;
	FileSystem::StreamCallback c0, c1, c2;
	c0.bind<&StreamResult::onStream>(&inside);
	c1.bind<&StreamResult::onStream>(&past);
	c2.bind<&StreamResult::onStream>(&huge);
	f.fs->stream(a, 0, 284, 16, c0);          // last 16 bytes of the entry
	f.fs->stream(a, 0, 290, 16, c1);          // crosses into the next entry, must fail
	f.fs->stream(a, 0, 8, ~(u64)0, c2);       // overflow, must fail
	ASSERT_TRUE(f.waitIdle());
	ASSERT_TRUE(inside.success);
	ASSERT_TRUE(!past.success);
	ASSERT_TRUE(!huge.success);

	f.fs->close(a);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemPakStreamMissingEntry() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("not_in_pak.bin"));
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 3, 0, 4, cb);
	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	return true;
}

bool testFileSystemPakStreamCompiledResource() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	// offsets of a compiled resource are in the data after its header
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("pak_raw.res"));
	StreamResult r, past;
	FileSystem::StreamCallback cb, cb_past;
	cb.bind<&StreamResult::onStream>(&r);
	cb_past.bind<&StreamResult::onStream>(&past);
	f.fs->stream(h, 1, 10, 16, cb);
	f.fs->stream(h, 2, 190, 16, cb_past); // 200 bytes of data, the header is not part of them
	ASSERT_TRUE(f.waitIdle());
	ASSERT_TRUE(r.success);
	ASSERT_EQ(16u, (u32)r.data.size());
	for (u32 i = 0; i < 16; ++i) ASSERT_EQ((u32)(u8)((10 + i) * 5 + 2), (u32)r.data[i]);
	ASSERT_TRUE(!past.success);

	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	return true;
}

bool testFileSystemPakStreamCompressedResourceFails() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("pak_packed.res"));
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 1, 0, 16, cb);
	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	return true;
}

static bool writeResource(FileSystem& fs, const char* path, u32 flags) {
	OutputMemoryStream data(getGlobalAllocator());
	CompiledResourceHeader header;
	header.flags = flags;
	data.write(&header, sizeof(header));
	for (u32 i = 0; i < 256; ++i) data.write((u8)expectedByte(i));
	return fs.saveContentSync(Path(path), Span<const u8>((const u8*)data.data(), (u32)data.size()));
}

bool testFileSystemStreamCompiledResource() {
	Fixture f;
	ASSERT_TRUE(f.created);
	ASSERT_TRUE(writeResource(*f.fs, "file_system_stream_test.res", 0));
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("file_system_stream_test.res"));
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 1, 20, 8, cb);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_TRUE(r.success);
	for (u32 i = 0; i < 8; ++i) ASSERT_EQ((u32)expectedByte(20 + i), (u32)r.data[i]);
	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	f.fs->deleteFile("file_system_stream_test.res");
	return true;
}

bool testFileSystemStreamCompressedResourceFails() {
	Fixture f;
	ASSERT_TRUE(f.created);
	ASSERT_TRUE(writeResource(*f.fs, "file_system_stream_test_packed.res", CompiledResourceHeader::COMPRESSED));
	const FileSystem::StreamHandle h = f.fs->openStreaming(Path("file_system_stream_test_packed.res"));
	StreamResult r;
	FileSystem::StreamCallback cb;
	cb.bind<&StreamResult::onStream>(&r);
	f.fs->stream(h, 1, 0, 8, cb);
	f.fs->close(h);
	ASSERT_TRUE(f.waitIdle());
	ASSERT_EQ(1u, r.call_count);
	ASSERT_TRUE(!r.success);
	f.fs->deleteFile("file_system_stream_test_packed.res");
	return true;
}

// bytes of the test files are (index * mul + add)
static bool hasPattern(const StreamResult& r, u32 offset, u32 count, u32 mul, u32 add) {
	if (!r.success || r.data.size() != count) return false;
	for (u32 i = 0; i < count; ++i) {
		if (r.data[i] != (u8)((offset + i) * mul + add)) return false;
	}
	return true;
}

bool testFileSystemStreamInterleavedStreams() {
	Fixture f;
	ASSERT_TRUE(f.created);
	static const char* OTHER_FILE = "file_system_stream_test_other.bin";
	u8 other[1024];
	for (u32 i = 0; i < 1024; ++i) other[i] = (u8)(i * 11 + 5);
	ASSERT_TRUE(f.fs->saveContentSync(Path(OTHER_FILE), Span<const u8>(other, 1024)));

	const FileSystem::StreamHandle a = f.fs->openStreaming(Path(TEST_FILE));
	const FileSystem::StreamHandle b = f.fs->openStreaming(Path(OTHER_FILE));
	ASSERT_TRUE(a != b);

	StreamResult r[6];
	FileSystem::StreamCallback cb[6];
	for (u32 i = 0; i < 6; ++i) cb[i].bind<&StreamResult::onStream>(&r[i]);
	// alternating reads, the first stream is closed while the second one still has reads queued
	f.fs->stream(a, 0, 0, 16, cb[0]);
	f.fs->stream(b, 1, 0, 16, cb[1]);
	f.fs->stream(a, 2, 100, 16, cb[2]);
	f.fs->stream(b, 3, 200, 16, cb[3]);
	f.fs->close(a);
	f.fs->stream(b, 4, 500, 16, cb[4]);
	f.fs->stream(b, 5, 1000, 24, cb[5]);
	f.fs->close(b);
	ASSERT_TRUE(f.waitIdle());

	for (u32 i = 0; i < 6; ++i) {
		ASSERT_EQ(1u, r[i].call_count);
		ASSERT_EQ(i, (u32)r[i].user_data);
	}
	ASSERT_TRUE(hasPattern(r[0], 0, 16, 7, 3));
	ASSERT_TRUE(hasPattern(r[1], 0, 16, 11, 5));
	ASSERT_TRUE(hasPattern(r[2], 100, 16, 7, 3));
	ASSERT_TRUE(hasPattern(r[3], 200, 16, 11, 5));
	ASSERT_TRUE(hasPattern(r[4], 500, 16, 11, 5));
	ASSERT_TRUE(hasPattern(r[5], 1000, 24, 11, 5));

	f.fs->deleteFile(OTHER_FILE);
	return true;
}

bool testFileSystemPakStreamInterleavedStreams() {
	PakFixture f;
	ASSERT_TRUE(f.created);
	const FileSystem::StreamHandle a = f.fs->openStreaming(Path("pak_a.bin"));
	const FileSystem::StreamHandle b = f.fs->openStreaming(Path("pak_b.bin"));

	StreamResult r[6];
	FileSystem::StreamCallback cb[6];
	for (u32 i = 0; i < 6; ++i) cb[i].bind<&StreamResult::onStream>(&r[i]);
	f.fs->stream(a, 0, 0, 16, cb[0]);
	f.fs->stream(b, 1, 0, 16, cb[1]);
	f.fs->stream(a, 2, 280, 20, cb[2]);
	f.fs->stream(b, 3, 480, 20, cb[3]);
	f.fs->close(a);
	f.fs->stream(b, 4, 250, 16, cb[4]);
	f.fs->stream(b, 5, 0, 500, cb[5]); // all of the entry
	f.fs->close(b);
	ASSERT_TRUE(f.waitIdle());

	for (u32 i = 0; i < 6; ++i) {
		ASSERT_EQ(1u, r[i].call_count);
		ASSERT_EQ(i, (u32)r[i].user_data);
	}
	// entry e has bytes (i * (e + 3) + e)
	ASSERT_TRUE(hasPattern(r[0], 0, 16, 3, 0));
	ASSERT_TRUE(hasPattern(r[1], 0, 16, 4, 1));
	ASSERT_TRUE(hasPattern(r[2], 280, 20, 3, 0));
	ASSERT_TRUE(hasPattern(r[3], 480, 20, 4, 1));
	ASSERT_TRUE(hasPattern(r[4], 250, 16, 4, 1));
	ASSERT_TRUE(hasPattern(r[5], 0, 500, 4, 1));
	return true;
}

bool testFileSystemStreamLeftOpenAtShutdown() {
	{
		Fixture f;
		ASSERT_TRUE(f.created);
		f.fs->openStreaming(Path(TEST_FILE));
		ASSERT_TRUE(f.waitIdle());
		// never closed, ~FileSystem must close the file or the fixture can't delete it / InputFile asserts
	}
	return true;
}

} // namespace

void runFileSystemTests() {
	RUN_TEST(testFileSystemGetContentAsync);
	RUN_TEST(testFileSystemStreamRead);
	RUN_TEST(testFileSystemStreamMultipleReads);
	RUN_TEST(testFileSystemStreamPastEof);
	RUN_TEST(testFileSystemStreamOpenMissingFile);
	RUN_TEST(testFileSystemStreamUsesMounts);
	RUN_TEST(testFileSystemStreamHandlesUnique);
	RUN_TEST(testFileSystemStreamAfterClose);
	RUN_TEST(testFileSystemStreamMixedWithGetContent);
	RUN_TEST(testFileSystemPakStreamRead);
	RUN_TEST(testFileSystemPakStreamBounds);
	RUN_TEST(testFileSystemPakStreamMissingEntry);
	RUN_TEST(testFileSystemPakStreamCompiledResource);
	RUN_TEST(testFileSystemPakStreamCompressedResourceFails);
	RUN_TEST(testFileSystemStreamCompiledResource);
	RUN_TEST(testFileSystemStreamCompressedResourceFails);
	RUN_TEST(testFileSystemStreamInterleavedStreams);
	RUN_TEST(testFileSystemPakStreamInterleavedStreams);
	RUN_TEST(testFileSystemStreamLeftOpenAtShutdown);
}
