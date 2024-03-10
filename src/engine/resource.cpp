#include "engine/lumix.h"

#include "core/hash.h"
#include "core/log.h"
#include "core/path.h"
#include "core/stream.h"
#include "core/string.h"

#include "engine/resource.h"
#include "engine/resource_manager.h"

#include "lz4/lz4.h"


namespace Lumix
{


ResourceType::ResourceType(const char* type_name)
{
	ASSERT(type_name[0] == 0 || (type_name[0] >= 'a' && type_name[0] <= 'z'));
	type = RuntimeHash(type_name);
#ifdef LUMIX_DEBUG
	str = type_name;
#endif
}


Resource::Resource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: m_ref_count(0)
	, m_empty_dep_count(1)
	, m_failed_dep_count(0)
	, m_current_state(State::EMPTY)
	, m_desired_state(State::EMPTY)
	, m_path(path)
	, m_file_size(0)
	, m_cb(allocator)
	, m_resource_manager(resource_manager)
	, m_async_op(FileSystem::AsyncHandle::invalid())
{
}


Resource::~Resource() = default;

void Resource::refresh() {
	if (m_current_state == State::EMPTY) return;

	const State old_state = m_current_state;
	m_current_state = State::EMPTY;	
	m_cb.invoke(old_state, m_current_state, *this);
	checkState();
}


void Resource::checkState()
{
	auto old_state = m_current_state;
	if (m_failed_dep_count > 0 && m_current_state != State::FAILURE)
	{
		m_current_state = State::FAILURE;
		m_cb.invoke(old_state, m_current_state, *this);
	}

	if (m_failed_dep_count == 0)
	{
		if (m_empty_dep_count == 0 && m_current_state != State::READY &&
			m_desired_state != State::EMPTY)
		{
			onBeforeReady();
			const bool state_changed = m_empty_dep_count != 0 
				|| m_current_state == State::READY 
				|| m_desired_state == State::EMPTY;
			
			if (state_changed) {
				return;
			}

			if (m_failed_dep_count != 0) {
				checkState();
				return;
			}

			m_current_state = State::READY;
			m_cb.invoke(old_state, m_current_state, *this);
		}

		if (m_empty_dep_count > 0 && m_current_state != State::EMPTY)
		{
			m_current_state = State::EMPTY;
			m_cb.invoke(old_state, m_current_state, *this);
		}
	}
}


void Resource::fileLoaded(Span<const u8> blob, bool success) {
	ASSERT(m_async_op.isValid());
	m_async_op = FileSystem::AsyncHandle::invalid();
	if (m_desired_state != State::READY) return;
	
	ASSERT(m_current_state != State::READY);
	ASSERT(m_empty_dep_count == 1);

	if (!success) {
		ResourceManagerHub& hub = m_resource_manager.getOwner();
		if (!m_hooked && hub.isHooked()) {
			if (hub.onBeforeLoad(*this) == ResourceManagerHub::LoadHook::Action::DEFERRED) {
				m_hooked = true;
				m_desired_state = Resource::State::READY;
				incRefCount(); // for hook
				logInfo("Trying to reload ", getPath(), " because resource manager has been hooked");
				return;
			}
		}

		logError("Could not open ", getPath());
		ASSERT(m_empty_dep_count > 0);
		--m_empty_dep_count;
		++m_failed_dep_count;
		checkState();
		m_async_op = FileSystem::AsyncHandle::invalid();
		return;
	}

	const CompiledResourceHeader* header = (const CompiledResourceHeader*)blob.begin();
	m_file_size = blob.length();
	if (startsWith(getPath(), ".lumix/asset_tiles/")) {
		if (!load(blob)) ++m_failed_dep_count;
	}
	else if (blob.length() < sizeof(*header)) {
		logError("Invalid resource file, please delete .lumix directory");
		++m_failed_dep_count;
	}
	else if (header->magic != CompiledResourceHeader::MAGIC) {
		logError("Invalid resource file, please delete .lumix directory");
		++m_failed_dep_count;
	}
	else if (header->version != 0) {
		logError("Unsupported resource file version, please delete .lumix directory");
		++m_failed_dep_count;
	}
	else if (header->flags & CompiledResourceHeader::COMPRESSED) {
		OutputMemoryStream tmp(m_resource_manager.m_allocator);
		tmp.resize(header->decompressed_size);
		const i32 res = LZ4_decompress_safe((const char*)blob.begin() + sizeof(*header), (char*)tmp.getMutableData(), i32(blob.length() - sizeof(*header)), (i32)tmp.size());
		if (res != header->decompressed_size || !load(tmp)) {
			++m_failed_dep_count;
		}
	}
	else {
		if (!load(blob.fromLeft(sizeof(*header)))) {
			++m_failed_dep_count;
		}
	} 

	ASSERT(m_empty_dep_count > 0);
	--m_empty_dep_count;
	checkState();
}


void Resource::doUnload()
{
	if (m_async_op.isValid())
	{
		FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
		fs.cancel(m_async_op);
		m_async_op = FileSystem::AsyncHandle::invalid();
	}

	m_hooked = false;
	m_desired_state = State::EMPTY;
	unload();
	ASSERT(m_empty_dep_count <= 1);

	m_file_size = 0;
	m_empty_dep_count = 1;
	m_failed_dep_count = 0;
	checkState();
}


void Resource::onCreated(State state)
{
	ASSERT(m_empty_dep_count == 1);
	ASSERT(m_failed_dep_count == 0);

	m_current_state = state;
	m_desired_state = State::READY;
	m_failed_dep_count = state == State::FAILURE ? 1 : 0;
	m_empty_dep_count = 0;
}


void Resource::doLoad()
{
	if (m_desired_state == State::READY) return;
	m_desired_state = State::READY;

	if (m_async_op.isValid()) return;

	ASSERT(m_current_state != State::READY);

	FileSystem& fs = m_resource_manager.getOwner().getFileSystem();
	FileSystem::ContentCallback cb = makeDelegate<&Resource::fileLoaded>(this);

	if (startsWith(m_path, ".lumix/asset_tiles/")) {
		m_async_op = fs.getContent(m_path, cb);
	}
	else {	
		const FilePathHash hash = m_path.getHash();
		const Path res_path(".lumix/resources/", hash, ".res");
		m_async_op = fs.getContent(res_path, cb);
	}
}


void Resource::addDependency(Resource& dependent_resource)
{
	ASSERT(m_desired_state != State::EMPTY);

	dependent_resource.m_cb.bind<&Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) ++m_empty_dep_count;
	if (dependent_resource.isFailure()) {
		++m_failed_dep_count;
	}

	checkState();
}


void Resource::removeDependency(Resource& dependent_resource)
{
	dependent_resource.m_cb.unbind<&Resource::onStateChanged>(this);
	if (dependent_resource.isEmpty()) 
	{
		ASSERT(m_empty_dep_count > 1 || (m_empty_dep_count == 1 && !m_async_op.isValid())); 
		--m_empty_dep_count;
	}
	if (dependent_resource.isFailure())
	{
		ASSERT(m_failed_dep_count > 0);
		--m_failed_dep_count;
	}

	checkState();
}

u32 Resource::decRefCount() {
	ASSERT(m_ref_count > 0);
	--m_ref_count;
	if (m_ref_count == 0 && m_resource_manager.m_is_unload_enabled) {
		doUnload();
	}
	return m_ref_count;
}


void Resource::onStateChanged(State old_state, State new_state, Resource&)
{
	ASSERT(old_state != new_state);
	ASSERT(m_current_state != State::EMPTY || m_desired_state != State::EMPTY);

	if (old_state == State::EMPTY)
	{
		ASSERT(m_empty_dep_count > 0);
		--m_empty_dep_count;
	}
	if (old_state == State::FAILURE)
	{
		ASSERT(m_failed_dep_count > 0);
		--m_failed_dep_count;
	}

	if (new_state == State::EMPTY) ++m_empty_dep_count;
	if (new_state == State::FAILURE) ++m_failed_dep_count;

	checkState();
}


} // namespace Lumix
