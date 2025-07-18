/* Icinga 2 | (c) 2012 Icinga GmbH | GPLv2+ */

#include "base/dictionary.hpp"
#include "base/debug.hpp"
#include "base/primitivetype.hpp"
#include "base/configwriter.hpp"
#include <sstream>

using namespace icinga;

template class std::map<String, Value>;

REGISTER_PRIMITIVE_TYPE(Dictionary, Object, Dictionary::GetPrototype());

Dictionary::Dictionary(const DictionaryData& other)
{
	for (const auto& kv : other)
		m_Data.insert(kv);
}

Dictionary::Dictionary(DictionaryData&& other)
{
	for (auto& kv : other)
		m_Data.insert(std::move(kv));
}

Dictionary::Dictionary(std::initializer_list<Dictionary::Pair> init)
	: m_Data(init)
{ }

/**
 * Retrieves a value from a dictionary.
 *
 * @param key The key whose value should be retrieved.
 * @returns The value of an empty value if the key was not found.
 */
Value Dictionary::Get(const String& key) const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	auto it = m_Data.find(key);

	if (it == m_Data.end())
		return Empty;

	return it->second;
}

/**
 * Retrieves a value from a dictionary.
 *
 * @param key The key whose value should be retrieved.
 * @param result The value of the dictionary item (only set when the key exists)
 * @returns true if the key exists, false otherwise.
 */
bool Dictionary::Get(const String& key, Value *result) const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	auto it = m_Data.find(key);

	if (it == m_Data.end())
		return false;

	*result = it->second;
	return true;
}

/**
 * Retrieves a value's address from a dictionary.
 *
 * @param key The key whose value's address should be retrieved.
 * @returns nullptr if the key was not found.
 */
const Value * Dictionary::GetRef(const String& key) const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);
	auto it (m_Data.find(key));

	return it == m_Data.end() ? nullptr : &it->second;
}

/**
 * Sets a value in the dictionary.
 *
 * @param key The key.
 * @param value The value.
 */
void Dictionary::Set(const String& key, Value value)
{
	ObjectLock olock(this);
	std::unique_lock<std::shared_timed_mutex> lock (m_DataMutex);

	if (m_Frozen)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Value in dictionary must not be modified."));

	m_Data[key] = std::move(value);
}

/**
 * Returns the number of elements in the dictionary.
 *
 * @returns Number of elements.
 */
size_t Dictionary::GetLength() const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	return m_Data.size();
}

/**
 * Checks whether the dictionary contains the specified key.
 *
 * @param key The key.
 * @returns true if the dictionary contains the key, false otherwise.
 */
bool Dictionary::Contains(const String& key) const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	return (m_Data.find(key) != m_Data.end());
}

/**
 * Returns an iterator to the beginning of the dictionary.
 *
 * Note: Caller must hold the object lock while using the iterator.
 *
 * @returns An iterator.
 */
Dictionary::Iterator Dictionary::Begin()
{
	ASSERT(Frozen() || OwnsLock());

	return m_Data.begin();
}

/**
 * Returns an iterator to the end of the dictionary.
 *
 * Note: Caller must hold the object lock while using the iterator.
 *
 * @returns An iterator.
 */
Dictionary::Iterator Dictionary::End()
{
	ASSERT(Frozen() || OwnsLock());

	return m_Data.end();
}

/**
 * Removes the item specified by the iterator from the dictionary.
 *
 * @param it The iterator.
 */
void Dictionary::Remove(Dictionary::Iterator it)
{
	ASSERT(OwnsLock());
	std::unique_lock<std::shared_timed_mutex> lock (m_DataMutex);

	if (m_Frozen)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Dictionary must not be modified."));

	m_Data.erase(it);
}

/**
 * Removes the specified key from the dictionary.
 *
 * @param key The key.
 */
void Dictionary::Remove(const String& key)
{
	ObjectLock olock(this);
	std::unique_lock<std::shared_timed_mutex> lock (m_DataMutex);

	if (m_Frozen)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Dictionary must not be modified."));

	Dictionary::Iterator it;
	it = m_Data.find(key);

	if (it == m_Data.end())
		return;

	m_Data.erase(it);
}

/**
 * Removes all dictionary items.
 */
void Dictionary::Clear()
{
	ObjectLock olock(this);
	std::unique_lock<std::shared_timed_mutex> lock (m_DataMutex);

	if (m_Frozen)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Dictionary must not be modified."));

	m_Data.clear();
}

void Dictionary::CopyTo(const Dictionary::Ptr& dest) const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	for (const Dictionary::Pair& kv : m_Data) {
		dest->Set(kv.first, kv.second);
	}
}

/**
 * Makes a shallow copy of a dictionary.
 *
 * @returns a copy of the dictionary.
 */
Dictionary::Ptr Dictionary::ShallowClone() const
{
	Dictionary::Ptr clone = new Dictionary();
	CopyTo(clone);
	return clone;
}

/**
 * Makes a deep clone of a dictionary
 * and its elements.
 *
 * @returns a copy of the dictionary.
 */
Object::Ptr Dictionary::Clone() const
{
	DictionaryData dict;

	{
		std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

		dict.reserve(GetLength());

		for (const Dictionary::Pair& kv : m_Data) {
			dict.emplace_back(kv.first, kv.second.Clone());
		}
	}

	return new Dictionary(std::move(dict));
}

/**
 * Returns an ordered vector containing all keys
 * which are currently set in this directory.
 *
 * @returns an ordered vector of key names
 */
std::vector<String> Dictionary::GetKeys() const
{
	std::shared_lock<std::shared_timed_mutex> lock (m_DataMutex);

	std::vector<String> keys;

	for (const Dictionary::Pair& kv : m_Data) {
		keys.push_back(kv.first);
	}

	return keys;
}

String Dictionary::ToString() const
{
	std::ostringstream msgbuf;
	ConfigWriter::EmitScope(msgbuf, 1, const_cast<Dictionary *>(this));
	return msgbuf.str();
}

void Dictionary::Freeze()
{
	ObjectLock olock(this);
	m_Frozen.store(true, std::memory_order_release);
}

bool Dictionary::Frozen() const
{
	return m_Frozen.load(std::memory_order_acquire);
}

/**
 * Returns an already locked ObjectLock if the dictionary is frozen.
 * Otherwise, returns an unlocked object lock.
 *
 * @returns An object lock.
 */
ObjectLock Dictionary::LockIfRequired()
{
	if (Frozen()) {
		return ObjectLock(this, std::defer_lock);
	}
	return ObjectLock(this);
}

Value Dictionary::GetFieldByName(const String& field, bool, const DebugInfo& debugInfo) const
{
	Value value;

	if (Get(field, &value))
		return value;
	else
		return GetPrototypeField(const_cast<Dictionary *>(this), field, false, debugInfo);
}

void Dictionary::SetFieldByName(const String& field, const Value& value, const DebugInfo&)
{
	Set(field, value);
}

bool Dictionary::HasOwnField(const String& field) const
{
	return Contains(field);
}

bool Dictionary::GetOwnField(const String& field, Value *result) const
{
	return Get(field, result);
}

Dictionary::Iterator icinga::begin(const Dictionary::Ptr& x)
{
	return x->Begin();
}

Dictionary::Iterator icinga::end(const Dictionary::Ptr& x)
{
	return x->End();
}
