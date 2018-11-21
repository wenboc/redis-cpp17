#include "table.h"
#include "format.h"
#include "coding.h"
#include "option.h"

struct Table::Rep
{
	~Rep()
	{

	}

	Options options;
	Status status;
	std::shared_ptr<RandomAccessFile> file;
	uint64_t cacheId;
	BlockHandle metaindexHandle;  // Handle to metaindex_block: saved from footer
	std::shared_ptr<Block> indexBlock;
};

Table::~Table()
{

}

Status Table::open(const Options &options,
	const std::shared_ptr<RandomAccessFile> &file,
	uint64_t size,
	std::shared_ptr<Table> &table)
{
	if (size < Footer::kEncodedLength)
	{
		return Status::corruption("file is too short to be an sstable");
	}
	
	char footerSpace[Footer::kEncodedLength];
	std::string_view footerInput;
	Status s = file->read(size - Footer::kEncodedLength, Footer::kEncodedLength,
					&footerInput, footerSpace);
	if (!s.ok()) return s;
	
	Footer footer;
	s = footer.decodeFrom(&footerInput);
	if (!s.ok()) return s;

	// Read the index block
	BlockContents indexBlockContents;
	if (s.ok()) 
	{
		ReadOptions opt;
		if (options.paranoidChecks) 
		{
			opt.verifyChecksums = true;
		}
		s = readBlock(file, opt, footer.getIndexHandle(), &indexBlockContents);
	}
  
	if (s.ok()) 
	{
		// We've successfully read the footer and the index block: we're
		// ready to serve requests.
		std::shared_ptr<Block> indexBlock(new Block(indexBlockContents));
		std::shared_ptr<Rep> rep(new Table::Rep);
		rep->options = options;
		rep->file = file;
		rep->metaindexHandle = footer.getMetaindexHandle();
		rep->indexBlock = indexBlock;
		table = std::shared_ptr<Table>(new Table(rep));
	}
	return s;
}

std::shared_ptr<BlockIterator> Table::blockReader(const std::any &arg,
	 const ReadOptions &options,
	 const std::string_view &indexValue)
{
	Table *table = std::any_cast<Table*>(arg);
	std::shared_ptr<Block> block = nullptr;
	BlockHandle handle;
	std::string_view input = indexValue;
	Status s = handle.decodeFrom(&input);
    if (s.ok()) 
	{
		BlockContents contents;
		s = readBlock(table->rep->file, options, handle, &contents);
		if (s.ok()) 
		{
			block = std::shared_ptr<Block>(new Block(contents));
		}
	}

	std::shared_ptr<BlockIterator> iter;
	if (block != nullptr)
	{
		iter = block->newIterator(table->rep->options.comparator.get());
		iter->registerCleanup(block);
	}
	else
	{
		iter = std::shared_ptr<BlockIterator>(new BlockIterator(s));
	}
	return iter;
}
								 
Status Table::internalGet(
	const ReadOptions &options, 
	const std::string_view &key,
	const std::any &arg,
	std::function<void(const std::any &arg, 
	const std::string_view &k, const std::string_view &v)> &callback)
{
	Status s;
	std::shared_ptr<BlockIterator> iter = rep->indexBlock->newIterator(rep->options.comparator.get());
	iter->seek(key);
	if (iter->valid())
	{
		std::shared_ptr<BlockIterator> blockIter = blockReader(this, options, iter->getValue());
		blockIter->seek(key);
		if (blockIter->valid()) 
		{
			callback(arg, blockIter->getKey(), blockIter->getValue());
		}
		s = blockIter->getStatus();
	}
	
	if (s.ok()) 
	{
		s = iter->getStatus();
	}
	return s;
}

uint64_t Table::approximateOffsetOf(const std::string_view &key) const
{
	std::shared_ptr<BlockIterator> indexIter = rep->indexBlock->newIterator(rep->options.comparator.get());
	indexIter->seek(key);
	uint64_t result;
	if (indexIter->valid())
	{
		BlockHandle handle;
		std::string_view input = indexIter->getValue();
		Status s = handle.decodeFrom(&input);
		if (s.ok())
		{
			result = handle.getOffset();
		}
		else
		{
			 // Strange: we can't decode the block handle in the index block.
			  // We'll just return the offset of the metaindex block, which is
			  // close to the whole file size for this case.
			  result = rep->metaindexHandle.getOffset();
		}
	}
	else
	{
		result = rep->metaindexHandle.getOffset();
	}
	return result;
}

