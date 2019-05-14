#include "dbiter.h"

#include "db.h"
#include "filename.h"
#include "dbformat.h"
#include "status.h"

// Memtables and sstables that make the DB representation contain
// (userkey,seq,type) => uservalue entries.  DBIter
// combines multiple entries for the same userkey found in the DB
// representation into a single entry while accounting for sequence
// numbers, deletion markers, overwrites, etc.
class DBIter : public Iterator {
public:
	// Which direction is the iterator currently moving?
	// (1) When moving forward, the internal iterator is positioned at
	//     the exact entry that yields this->key(), this->value()
	// (2) When moving backwards, the internal iterator is positioned
	//     just before all entries whose user key == this->key().
	enum Direction {
		kForward,
		kReverse
	};

	DBIter(DB* db, const Comparator* cmp,
		const std::shared_ptr<Iterator>& iter, uint64_t s,
		uint32_t seed)
		: db(db),
		comparator(cmp),
		iter(iter),
		sequence(s),
		direction(kForward),
		vali(false) {

	}

	virtual ~DBIter() {

	}

	virtual bool Valid() const { return vali; }

	virtual std::string_view key() const {
		assert(vali);
		return (direction == kForward) ? ExtractUserKey(iter->key()) : savedKey;
	}

	virtual std::string_view value() const {
		assert(vali);
		return (direction == kForward) ? iter->value() : savedValue;
	}

	virtual Status status() const {
		if (s.ok()) {
			return iter->status();
		}
		else {
			return s;
		}
	}

	virtual void Next();

	virtual void Prev();

	virtual void Seek(const std::string_view& target);

	virtual void SeekToFirst();

	virtual void SeekToLast();

	virtual void RegisterCleanup(const std::any& arg) {}

private:
	void findNextUserEntry(bool skipping, std::string* Skip);

	void findPrevUserEntry();

	bool parseKey(ParsedInternalKey* key);

	inline void saveKey(const std::string_view& k, std::string* dst) {
		dst->assign(k.data(), k.size());
	}

	inline void clearSavedValue() {
		if (savedValue.capacity() > 1048576) {
			std::string empty;
			std::swap(empty, savedValue);
		}
		else {
			savedValue.clear();
		}
	}

	DB* db;
	const Comparator* const comparator;
	std::shared_ptr<Iterator> const iter;
	uint64_t const sequence;

	Status s;
	std::string savedKey;     // == curren-t key when direction_==kReverse
	std::string savedValue;   // == current raw value when direction_==kReverse
	Direction direction;
	bool vali;

	std::default_random_engine random;

	// No copying allowed
	DBIter(const DBIter&);

	void operator=(const DBIter&);
};


inline bool DBIter::parseKey(ParsedInternalKey* ikey) {
	std::string_view k = iter->key();
	if (!ParseInternalKey(k, ikey)) {
		s = Status::Corruption("corrupted internal key in DBIter");
		return false;
	}
	else {
		return true;
	}
}

void DBIter::Next() {
	assert(vali);

	if (direction == kReverse) {
		// Switch directions?
		direction = kForward;
		// iter_ is pointing just before the entries for this->key(),
		// so advance into the range of entries for this->key() and then
		// use the normal skipping code below.
		if (!iter->Valid()) {
			iter->SeekToFirst();
		}
		else {
			iter->Next();
		}

		if (!iter->Valid()) {
			vali = false;
			savedKey.clear();
			return;
		}
		// saved_key_ already Contains the key to Skip past.
	}
	else {
		// Store in saved_key_ the current key so we Skip it below.
		saveKey(ExtractUserKey(iter->key()), &savedKey);
	}

	findNextUserEntry(true, &savedKey);
}

void DBIter::findNextUserEntry(bool skipping, std::string* Skip) {
	// Loop until we hit an acceptable entry to yield
	assert(iter->Valid());
	assert(direction == kForward);
	do {
		ParsedInternalKey ikey;
		if (parseKey(&ikey) && ikey.sequence<= sequence) {
			switch (ikey.type) {
			case kTypeDeletion:
				// Arrange to Skip all upcoming entries for this key since
				// they are hidden by this deletion.
				saveKey(ikey.userKey, Skip);
				skipping = true;
				break;
			case kTypeValue:
				if (skipping &&
					ikey.userKey.compare(std::string_view(*Skip))<= 0) {
					// Entry hidden
				}
				else {
					vali = true;
					savedKey.clear();
					return;
				}
				break;
			}
		}
		iter->Next();
	} while (iter->Valid());

	savedKey.clear();
	vali = false;
}

void DBIter::Prev() {
	assert(vali);

	if (direction == kForward) {  // Switch directions?
		// iter_ is pointing at the current entry.  Scan backwards until
		// the key changes so we can use the normal reverse scanning code.
		assert(iter->Valid());  // Otherwise valid_ would have been false
		saveKey(ExtractUserKey(iter->key()), &savedKey);
		while (true) {
			iter->Prev();
			if (!iter->Valid()) {
				vali = false;
				savedKey.clear();
				clearSavedValue();
				return;
			}
			if (comparator->Compare(ExtractUserKey(iter->key()),
				savedKey)< 0) {
				break;
			}
		}
		direction = kReverse;
	}

	findPrevUserEntry();
}

void DBIter::findPrevUserEntry() {
	assert(direction == kReverse);

	ValueType valueType = kTypeDeletion;
	if (iter->Valid()) {
		do {
			ParsedInternalKey ikey;
			if (parseKey(&ikey) && ikey.sequence<= sequence) {
				if ((valueType != kTypeDeletion) &&
					comparator->Compare(ikey.userKey, savedKey)< 0) {
					// We encountered a non-deleted value in entries for previous keys,
					break;
				}

				valueType = ikey.type;
				if (valueType == kTypeDeletion) {
					savedKey.clear();
					clearSavedValue();
				}
				else {
					std::string_view rawValue = iter->value();
					if (savedValue.capacity() > rawValue.size() + 1048576) {
						std::string empty;
						std::swap(empty, savedValue);
					}

					saveKey(ExtractUserKey(iter->key()), &savedKey);
					savedValue.assign(rawValue.data(), rawValue.size());
				}
			}
			iter->Prev();
		} while (iter->Valid());
	}

	if (valueType == kTypeDeletion) {
		// End
		vali = false;
		savedKey.clear();
		clearSavedValue();
		direction = kForward;
	}
	else {
		vali = true;
	}
}

void DBIter::Seek(const std::string_view& target) {
	direction = kForward;
	clearSavedValue();
	savedKey.clear();
	AppendInternalKey(
		&savedKey, ParsedInternalKey(target, sequence, kValueTypeForSeek));
	iter->Seek(savedKey);
	if (iter->Valid()) {
		findNextUserEntry(false, &savedKey /* temporary storage */);
	}
	else {
		vali = false;
	}
}

void DBIter::SeekToFirst() {
	direction = kForward;
	clearSavedValue();
	iter->SeekToFirst();
	if (iter->Valid()) {
		findNextUserEntry(false, &savedKey /* temporary storage */);
	}
	else {
		vali = false;
	}
}

void DBIter::SeekToLast() {
	direction = kReverse;
	clearSavedValue();
	iter->SeekToLast();
	findPrevUserEntry();
}

std::shared_ptr<Iterator> NewDBIterator(DB* db,
	const Comparator* userCmp,
	std::shared_ptr<Iterator> internalIter,
	uint64_t sequence,
	uint32_t seed) {
	std::shared_ptr<Iterator> iter(new DBIter(db, userCmp, internalIter, sequence, seed));
	return iter;
}

