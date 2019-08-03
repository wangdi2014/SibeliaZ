#ifndef _TRASERVAL_H_
#define _TRAVERSAL_H_

//#define _DEBUG_OUT_

#include <set>
#include <map>
#include <list>
#include <ctime>
#include <queue>
#include <iterator>
#include <cassert>
#include <numeric>
#include <sstream>
#include <iostream>
#include <functional>
#include <unordered_map>

#include <tbb/parallel_for.h>

#include "path.h"

namespace Sibelia
{
	extern const std::string DELIMITER;
	extern const std::string VERSION;

	class BlockInstance
	{
	public:
		BlockInstance() {}
		BlockInstance(int id, const size_t chr, size_t start, size_t end) : id_(id), chr_(chr), start_(start), end_(end) {}
		void Reverse();
		int GetSignedBlockId() const;
		bool GetDirection() const;
		int GetBlockId() const;
		int GetSign() const;
		size_t GetChrId() const;
		size_t GetStart() const;
		size_t GetEnd() const;
		size_t GetLength() const;
		bool operator < (const BlockInstance & toCompare) const;
		bool operator == (const BlockInstance & toCompare) const;
		bool operator != (const BlockInstance & toCompare) const;
	private:
		int id_;
		size_t start_;
		size_t end_;
		size_t chr_;
	};

	namespace
	{
		const bool COVERED = true;
		typedef std::vector<BlockInstance> BlockList;
		typedef std::pair<size_t, std::vector<BlockInstance> > GroupedBlock;
		typedef std::vector<GroupedBlock> GroupedBlockList;
		bool ByFirstElement(const GroupedBlock & a, const GroupedBlock & b)
		{
			return a.first < b.first;
		}

		std::string IntToStr(size_t x)
		{
			std::stringstream ss;
			ss << x;
			return ss.str();
		}

		template<class Iterator1, class Iterator2>
		void CopyN(Iterator1 it, size_t count, Iterator2 out)
		{
			for (size_t i = 0; i < count; i++)
			{
				*out++ = *it++;
			}
		}

		template<class Iterator>
		Iterator AdvanceForward(Iterator it, size_t step)
		{
			std::advance(it, step);
			return it;
		}

		template<class Iterator>
		Iterator AdvanceBackward(Iterator it, size_t step)
		{
			for (size_t i = 0; i < step; i++)
			{
				--it;
			}

			return it;
		}


		typedef std::pair<size_t, size_t> IndexPair;
		template<class T, class F, class It>
		void GroupBy(std::vector<T> & store, F pred, It out)
		{
			sort(store.begin(), store.end(), pred);
			for (size_t now = 0; now < store.size();)
			{
				size_t prev = now;
				for (; now < store.size() && !pred(store[prev], store[now]); now++);
				*out++ = std::make_pair(prev, now);
			}
		}

		template<class F>
		bool CompareBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return (a.*f)() < (b.*f)();
		}

		template<class F>
		bool EqualBlocks(const BlockInstance & a, const BlockInstance & b, F f)
		{
			return f(a) == f(b);
		}

		template<class Iterator, class F, class ReturnType>
		struct FancyIterator : public std::iterator<std::forward_iterator_tag, ReturnType>
		{
		public:
			FancyIterator& operator++()
			{
				++it;
				return *this;
			}

			FancyIterator operator++(int)
			{
				FancyIterator ret(*this);
				++(*this);
				return ret;
			}

			bool operator == (FancyIterator toCompare) const
			{
				return it == toCompare.it;
			}

			bool operator != (FancyIterator toCompare) const
			{
				return !(*this == toCompare);
			}

			ReturnType operator * ()
			{
				return f(*it);
			}

			FancyIterator() {}
			FancyIterator(Iterator it, F f) : it(it), f(f) {}

		private:
			F f;
			Iterator it;
		};

		template<class Iterator, class F, class ReturnType>
		FancyIterator<Iterator, F, ReturnType> CFancyIterator(Iterator it, F f, ReturnType)
		{
			return FancyIterator<Iterator, F, ReturnType>(it, f);
		}

	}

	bool compareById(const BlockInstance & a, const BlockInstance & b);
	bool compareByChrId(const BlockInstance & a, const BlockInstance & b);
	bool compareByStart(const BlockInstance & a, const BlockInstance & b);

	void CreateOutDirectory(const std::string & path);

	class BlocksFinder
	{
	public:

		BlocksFinder(JunctionStorage & storage, size_t k) : storage_(storage), k_(k)
		{
			progressCount_ = 50;
			scoreFullChains_ = true;			
		}

		struct ProcessVertex
		{
		public:
			BlocksFinder & finder;
			std::vector<int64_t> & shuffle;

			ProcessVertex(BlocksFinder & finder, std::vector<int64_t> & shuffle) : finder(finder), shuffle(shuffle)
			{
			}

			void operator()(tbb::blocked_range<size_t> & range) const
			{
				std::vector<size_t> data;
				std::vector<uint32_t> count(finder.storage_.GetVerticesNumber() * 2 + 1, 0);
				std::pair<int64_t, std::vector<Path::Instance> > goodInstance;
				Path finalizer(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				Path currentPath(finder.storage_, finder.maxBranchSize_, finder.minBlockSize_, finder.minBlockSize_, finder.maxFlankingSize_);
				for (size_t i = range.begin(); i != range.end(); i++)
				{
					if (finder.count_++ % finder.progressPortion_ == 0)
					{
						finder.progressMutex_.lock();
						std::cout << '.' << std::flush;
						finder.progressMutex_.unlock();
					}

					int64_t score;
					int64_t vid = shuffle[i];
#ifdef _DEBUG_OUT_
					finder.debug_ = finder.missingVertex_.count(vid);
					if (finder.debug_)
					{
						std::cerr << "Vid: " << vid << std::endl;
					}
#endif
					for (bool explore = true; explore;)
					{
						currentPath.Init(vid);
						if (currentPath.AllInstances().size() < 2)
						{
							currentPath.Clear();
							break;
						}

						int64_t bestScore = 0;
						size_t bestRightSize = currentPath.RightSize();
						size_t bestLeftSize = currentPath.LeftSize();
#ifdef _DEBUG_OUT_
						if (finder.debug_)
						{
							std::cerr << "Going forward:" << std::endl;
						}
#endif
						int64_t minRun = max(finder.minBlockSize_, finder.maxBranchSize_) * 2;
						while (true)
						{
							bool ret = true;
							bool positive = false;
							int64_t prevLength = currentPath.MiddlePathLength();
							while ((ret = finder.ExtendPathForward(currentPath, count, data, bestRightSize, bestScore, score)) && currentPath.MiddlePathLength() - prevLength <= minRun)
							{
								positive = positive || (score > 0);
							}

							if (!ret || !positive)
							{
								break;
							}
						}

						if (bestRightSize == 1)
						{
							break;
						}

						{
							std::vector<Edge> bestEdge;
							for (size_t i = 0; i < bestRightSize - 1; i++)
							{
								bestEdge.push_back(currentPath.RightPoint(i).GetEdge());
							}

							currentPath.Clear();
							currentPath.Init(vid);
							for (auto & e : bestEdge)
							{
								currentPath.PointPushBack(e);
							}
						}
#ifdef _DEBUG_OUT_
						if (finder.debug_)
						{
							std::cerr << "Going backward:" << std::endl;
						}
#endif
						while (true)
						{
							bool ret = true;
							bool positive = false;
							int64_t prevLength = currentPath.MiddlePathLength();
							while ((ret = finder.ExtendPathBackward(currentPath, count, data, bestLeftSize, bestScore, score)) && currentPath.MiddlePathLength() - prevLength <= minRun);
							{
								positive = positive || (score > 0);
							}

							if (!ret || !positive)
							{
								break;
							}
						}
						
						if (bestScore > 0)
						{
#ifdef _DEBUG_OUT_
							if (finder.debug_)
							{
								std::cerr << "Setting a new block. Best score:" << bestScore << std::endl;
								currentPath.DumpPath(std::cerr);
								currentPath.DumpInstances(std::cerr);
							}
#endif
							if (!finder.TryFinalizeBlock(currentPath, finalizer, bestRightSize, bestLeftSize))
							{
								explore = false;
							}
						}
						else
						{
							explore = false;
						}

						currentPath.Clear();
					}
				}
			}
		};

		static bool DegreeCompare(const JunctionStorage & storage, int64_t v1, int64_t v2)
		{
			return storage.GetInstancesCount(v1) > storage.GetInstancesCount(v2);
		}

		void Split(std::string & source, std::vector<std::string> & result)
		{
			std::stringstream ss;
			ss << source;
			result.clear();
			while (ss >> source)
			{
				result.push_back(source);
			}
		}

		void FindBlocks(int64_t minBlockSize, int64_t maxBranchSize, int64_t maxFlankingSize, int64_t lookingDepth, int64_t sampleSize, int64_t threads, const std::string & debugOut)
		{
			blocksFound_ = 0;
			sampleSize_ = sampleSize;
			lookingDepth_ = lookingDepth;
			minBlockSize_ = minBlockSize;
			maxBranchSize_ = maxBranchSize;
			maxFlankingSize_ = maxFlankingSize;

			std::vector<int64_t> shuffle;
			for (int64_t v = -storage_.GetVerticesNumber() + 1; v < storage_.GetVerticesNumber(); v++)
			{
				for (JunctionStorage::JunctionIterator it(v); it.Valid(); ++it)
				{
					if (it.IsPositiveStrand())
					{
						shuffle.push_back(v);
						break;
					}
				}
			}

			using namespace std::placeholders;
			std::random_shuffle(shuffle.begin(), shuffle.end());

			time_t mark = time(0);
			count_ = 0;
			std::cout << '[' << std::flush;
			progressPortion_ = shuffle.size() / progressCount_;
			if (progressPortion_ == 0)
			{
				progressPortion_ = 1;
			}

			tbb::task_scheduler_init init(static_cast<int>(threads));
			/*
			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), CheckIfSource(*this, shuffle));
			tbb::parallel_for(tbb::blocked_range<size_t>(0, source_.size()), ProcessVertex(*this, source_));
			tbb::parallel_for(tbb::blocked_range<size_t>(0, sink_.size()), ProcessVertex(*this, sink_));
			*/
			std::sort(shuffle.begin(), shuffle.end(), std::bind(DegreeCompare, std::ref(storage_), _1, _2));
			tbb::parallel_for(tbb::blocked_range<size_t>(0, shuffle.size()), ProcessVertex(*this, shuffle));
			std::cout << ']' << std::endl;
			//storage_.DebugUsed();
	
			//std::cout << "Time: " << time(0) - mark << std::endl;
		}

		
		void ListBlocksSequences(const BlockList & block, const std::string & directory) const
		{
			std::vector<IndexPair> group;
			BlockList blockList = block;
			GroupBy(blockList, compareById, std::back_inserter(group));
			for (std::vector<IndexPair>::iterator it = group.begin(); it != group.end(); ++it)
			{
				std::ofstream out;
				std::stringstream ss;
				ss << directory << "/" << blockList[it->first].GetBlockId() << ".fa";
				TryOpenFile(ss.str(), out);
				for (size_t block = it->first; block < it->second; block++)
				{
					size_t length = blockList[block].GetLength();
					size_t chr = blockList[block].GetChrId();
					size_t chrSize = storage_.GetChrSequence(chr).size();
					out << ">" << blockList[block].GetBlockId() << "_" << block - it->first << " ";
					out << storage_.GetChrDescription(chr) << ";";
					if (blockList[block].GetSignedBlockId() > 0)
					{
						out << blockList[block].GetStart() << ";" << length << ";" << "+;" << chrSize << std::endl;
						OutputLines(storage_.GetChrSequence(chr).begin() + blockList[block].GetStart(), length, out);
					}
					else
					{
						size_t start = chrSize - blockList[block].GetEnd();
						out << start << ";" << length << ";" << "-;" << chrSize << std::endl;
						std::string::const_reverse_iterator it(storage_.GetChrSequence(chr).begin() + blockList[block].GetEnd());
						OutputLines(CFancyIterator(it, TwoPaCo::DnaChar::ReverseChar, ' '), length, out);
					}

					out << std::endl;
				}
			}
		}

		struct SortByMultiplicity
		{
			SortByMultiplicity(const std::vector<int> & multiplicityOrigin): multiplicity(multiplicityOrigin)
			{
			}

			bool operator () (const BlockInstance & a, const BlockInstance & b) const
			{
				auto mlp1 = multiplicity[a.GetBlockId()];
				auto mlp2 = multiplicity[b.GetBlockId()];
				if (mlp1 != mlp2)
				{
					return mlp1 > mlp2;
				}

				return a.GetBlockId() < b.GetBlockId();
			}

			const std::vector<int> & multiplicity;
		};

		void GenerateOutput(const std::string & outDir, bool genSeq)
		{
			std::vector<std::vector<bool> > covered(storage_.GetChrNumber());
			for (size_t i = 0; i < covered.size(); i++)
			{
				covered[i].assign(storage_.GetChrSequence(i).size() + 1, false);
			}
		
			int64_t trimmedId = 1;
			std::vector<IndexPair> group;
			std::vector<BlockInstance> buffer;
			std::vector<BlockInstance> trimmedBlocks;
			std::vector<int> copiesCount_(blocksFound_ + 1, 0);
			for (auto b : blocksInstance_)
			{
				copiesCount_[b.GetBlockId()]++;
			}

			GroupBy(blocksInstance_, SortByMultiplicity(copiesCount_), std::back_inserter(group));
			for (auto g : group)
			{
				buffer.clear();
				for (size_t i = g.first; i < g.second; i++)
				{
					size_t chr = blocksInstance_[i].GetChrId();
					size_t start = blocksInstance_[i].GetStart();
					size_t end = blocksInstance_[i].GetEnd();
					for (; covered[chr][start] && start < end; start++);
					for (; covered[chr][end] && end > start; end--);
					if (end - start >= minBlockSize_)
					{
						buffer.push_back(BlockInstance(blocksInstance_[i].GetSign() * trimmedId, chr, start, end));
						std::fill(covered[chr].begin() + start, covered[chr].begin() + end, true);
					}
				}

				if (buffer.size() > 1)
				{
					trimmedId++;
					for (const auto & it : buffer)
					{
						trimmedBlocks.push_back(it);
					}
				}
				else
				{
					for (const auto & it : buffer)
					{
						std::fill(covered[it.GetChrId()].begin() + it.GetStart(), covered[it.GetChrId()].begin() + it.GetEnd(), false);
					}					
				}
			}

			std::cout.setf(std::cout.fixed);
			std::cout.precision(2);
			std::cout << "Blocks found: " << blocksFound_ << std::endl;
			std::cout << "Coverage: " << CalculateCoverage(trimmedBlocks) << std::endl;

			CreateOutDirectory(outDir);
			std::string blocksDir = outDir + "/blocks";
			ListBlocksIndicesGFF(trimmedBlocks, outDir + "/" + "blocks_coords.gff");
			if (genSeq)
			{
				CreateOutDirectory(blocksDir);
				ListBlocksSequences(trimmedBlocks, blocksDir);
			}
		}


	private:

		template<class Iterator>
		void OutputLines(Iterator start, size_t length, std::ostream & out) const
		{
			for (size_t i = 1; i <= length; i++, ++start)
			{
				out << *start;
				if (i % 80 == 0 && i != length)
				{
					out << std::endl;
				}
			}
		}

		double CalculateCoverage(const BlockList & block) const;
		void ListBlocksIndicesGFF(BlockList & blockList, const std::string & fileName);
		void TryOpenFile(const std::string & fileName, std::ofstream & stream) const;

		template<class T>
		void DumpVertex(int64_t id, std::ostream & out, T & visit, int64_t cnt = 5) const
		{
			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto jt = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto it = jt - 1;
					auto pr = std::make_pair(it, jt);
					if (it.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					jt = it;
				}
			}

			for (auto kt = JunctionStorage::JunctionIterator(id); kt.Valid(); ++kt)
			{
				auto it = kt.SequentialIterator();
				for (int64_t i = 0; i < cnt; i++)
				{
					auto jt = it + 1;
					auto pr = std::make_pair(it, jt);
					if (jt.Valid() && std::find(visit.begin(), visit.end(), pr) == visit.end())
					{
						int64_t length = it.GetPosition() - jt.GetPosition();
						out << it.GetVertexId() << " -> " << jt.GetVertexId()
							<< "[label=\"" << it.GetChar() << ", " << it.GetChrId() << ", " << it.GetPosition() << "," << length << "\""
							<< (it.IsPositiveStrand() ? "color=blue" : "color=red") << "]\n";
						visit.push_back(pr);
					}

					it = jt;
				}
			}
		}

		bool TryFinalizeBlock(const Path & currentPath, Path & finalizer, size_t bestRightSize, size_t bestLeftSize)
		{	
			bool ret = false;
			std::vector<Path::InstanceSet::const_iterator> lockInstance;
			for (auto it : currentPath.GoodInstancesList())
			{
				lockInstance.push_back(it);
			}
			
			std::sort(lockInstance.begin(), lockInstance.end(), Path::CmpInstance);
			{
				std::pair<size_t, size_t> idx(SIZE_MAX, SIZE_MAX);
				for (auto & instance : lockInstance)
				{
					if (instance->Front().IsPositiveStrand())
					{
						storage_.LockRange(instance->Front(), instance->Back(), idx);
					}
					else
					{
						storage_.LockRange(instance->Back().Reverse(), instance->Front().Reverse(), idx);
					}
				}
			}
		
			finalizer.Init(currentPath.Origin());
			for (size_t i = 0; i < bestRightSize - 1 && finalizer.PointPushBack(currentPath.RightPoint(i).GetEdge()); i++);
			for (size_t i = 0; i < bestLeftSize - 1 && finalizer.PointPushFront(currentPath.LeftPoint(i).GetEdge()); i++);

			int64_t finalScore = finalizer.Score();
			int64_t finalInstances = finalizer.GoodInstances();
			if (finalScore > 0 && finalInstances > 1)
			{
				ret = true;				
				int64_t currentBlock = ++blocksFound_;				
				for (auto jt : finalizer.AllInstances())
				{
					if (finalizer.IsGoodInstance(*jt))
					{
						blocksMutex_.lock();
						if (jt->Front().IsPositiveStrand())
						{
							blocksInstance_.push_back(BlockInstance(+currentBlock, jt->Front().GetChrId(), jt->Front().GetPosition(), jt->Back().GetPosition() + k_));
						}
						else
						{
							blocksInstance_.push_back(BlockInstance(-currentBlock, jt->Front().GetChrId(), jt->Back().GetPosition() - k_, jt->Front().GetPosition()));
						}

						blocksMutex_.unlock();
						for(auto it = jt->Front(); it != jt->Back(); ++it)
						{						
							it.MarkUsed();
						}
					}
				}
			}

			finalizer.Clear();
			std::pair<size_t, size_t> idx(SIZE_MAX, SIZE_MAX);
			for (auto & instance : lockInstance)
			{
				if (instance->Front().IsPositiveStrand())
				{
					storage_.UnlockRange(instance->Front(), instance->Back(), idx);
				}
				else
				{
					storage_.UnlockRange(instance->Back().Reverse(), instance->Front().Reverse(), idx);
				}
			}

			return ret;
		}

		struct NextVertex
		{
			int64_t diff;
			int64_t count;
			JunctionStorage::JunctionSequentialIterator origin;
			NextVertex() : count(0)
			{

			}

			NextVertex(int64_t diff, JunctionStorage::JunctionSequentialIterator origin) : origin(origin), diff(diff), count(1)
			{

			}
		};

		std::pair<int64_t, NextVertex> MostPopularVertex(const Path & currentPath, bool forward, std::vector<uint32_t> & count, std::vector<size_t> & data)
		{
			NextVertex ret;
			int64_t bestVid = 0;
			int64_t startVid = forward ? currentPath.RightVertex() : currentPath.LeftVertex();
			const auto & instList = currentPath.GoodInstancesList().size() >= 2 ? currentPath.GoodInstancesList() : currentPath.AllInstances();
			for (auto & inst : instList)
			{
				int64_t nowVid = forward ? inst->Back().GetVertexId() : inst->Front().GetVertexId();
				if (nowVid == startVid)
				{
					int64_t weight = abs(inst->Front().GetPosition() - inst->Back().GetPosition()) + 1;
					auto origin = forward ? inst->Back() : inst->Front();
					auto it = forward ? origin.Next() : origin.Prev();
					for (size_t d = 1; it.Valid() && (d < size_t(lookingDepth_)  || abs(it.GetPosition() - origin.GetPosition()) <= maxBranchSize_); d++)
					{
						int64_t vid = it.GetVertexId();
						if (!currentPath.IsInPath(vid) && !it.IsUsed())
						{
							auto adjVid = vid + storage_.GetVerticesNumber();
							if (count[adjVid] == 0)
							{
								data.push_back(adjVid);
							}

							count[adjVid] += static_cast<uint32_t>(weight);
							auto diff = abs(it.GetAbsolutePosition() - origin.GetAbsolutePosition());
							if (count[adjVid] > ret.count || (count[adjVid] == ret.count && diff < ret.diff))
							{
								ret.diff = diff;
								ret.origin = origin;
								ret.count = count[adjVid];
								bestVid = vid;
							}
						}
						else
						{
							break;
						}

						if (forward)
						{
							++it;
						}
						else
						{
							--it;
						}
					}
				}
			}


			for (auto vid : data)
			{
				count[vid] = 0;
			}

			data.clear();
			return std::make_pair(bestVid, ret);
		}

		bool ExtendPathForward(Path & currentPath,
			std::vector<uint32_t> & count,
			std::vector<size_t> & data,
			size_t & bestRightSize,
			int64_t & bestScore,
			int64_t & nowScore)
		{
			bool success = false;
			int64_t origin = currentPath.Origin();
			std::pair<int64_t, NextVertex> nextForwardVid;
			nextForwardVid = MostPopularVertex(currentPath, true, count, data);
			if (nextForwardVid.first != 0)
			{
				for (auto it = nextForwardVid.second.origin; it.GetVertexId() != nextForwardVid.first; ++it)
				{
#ifdef _DEBUG_OUT_
					if (debug_)
					{
						std::cerr << "Attempting to push back the vertex:" << it.GetVertexId() << std::endl;
					}

					if (missingVertex_.count(it.GetVertexId()))
					{
						std::cerr << "Alert: " << it.GetVertexId() << ", origin: " << currentPath.Origin() << std::endl;
					}
#endif
					success = currentPath.PointPushBack(it.OutgoingEdge());
					if (success)
					{
						nowScore = currentPath.Score(scoreFullChains_);
#ifdef _DEBUG_OUT_
						if (debug_)
						{
							std::cerr << "Success! New score:" << nowScore << std::endl;
							currentPath.DumpPath(std::cerr);
							currentPath.DumpInstances(std::cerr);
						}
#endif												
						if (nowScore > bestScore)
						{
							bestScore = nowScore;
							bestRightSize = currentPath.RightSize();
						}
					}
				}
			}

			return success;
		}

		bool ExtendPathBackward(Path & currentPath,
			std::vector<uint32_t> & count,
			std::vector<size_t> & data,
			size_t & bestLeftSize,
			int64_t & bestScore,
			int64_t & nowScore)
		{
			bool success = false;
			std::pair<int64_t, NextVertex> nextBackwardVid;
			nextBackwardVid = MostPopularVertex(currentPath, false, count, data);
			if (nextBackwardVid.first != 0)
			{
				for (auto it = nextBackwardVid.second.origin; it.GetVertexId() != nextBackwardVid.first; --it)
				{
#ifdef _DEBUG_OUT_
					if (debug_)
					{
						std::cerr << "Attempting to push front the vertex:" << it.GetVertexId() << std::endl;
					}

					if (missingVertex_.count(it.GetVertexId()))
					{
						std::cerr << "Alert: " << it.GetVertexId() << ", origin: " << currentPath.Origin() << std::endl;
					}
#endif
					success = currentPath.PointPushFront(it.IngoingEdge());
					if (success)
					{
						nowScore = currentPath.Score(scoreFullChains_);
#ifdef _DEBUG_OUT_
						if (debug_)
						{
							std::cerr << "Success! New score:" << nowScore << std::endl;
							currentPath.DumpPath(std::cerr);
							currentPath.DumpInstances(std::cerr);
						}
#endif		
						if (nowScore > bestScore)
						{
							bestScore = nowScore;
							bestLeftSize = currentPath.LeftSize();
						}
					}
				}
			}

			return success;
		}

		struct BranchData
		{
			std::vector<size_t> branchId;
		};

		typedef std::vector<std::vector<size_t> > BubbledBranches;

		struct Fork
		{
			Fork(JunctionStorage::JunctionSequentialIterator it, JunctionStorage::JunctionSequentialIterator jt)
			{
				if (it < jt)
				{
					branch[0] = it;
					branch[1] = jt;
				}
				else
				{
					branch[0] = jt;
					branch[1] = it;
				}
			}

			bool operator == (const Fork & f) const
			{
				return branch[0] == f.branch[0] && branch[1] == f.branch[1];
			}

			bool operator != (const Fork & f) const
			{
				return !(*this == f);
			}

			std::string ToString() const
			{
				std::stringstream ss;
				for (size_t l = 0; l < 2; l++)
				{
					ss << (branch[l].IsPositiveStrand() ? '+' : '-') << ' ' << branch[l].GetChrId() << ' ' << branch[l].GetPosition() << ' ' << branch[l].GetChar() << ' ' << branch[l].GetVertexId() << "; ";
				}

				return ss.str();
			}

			bool operator < (const Fork & f) const
			{
				//return branch[0] < f.branch[0];
				return std::make_pair(branch[0], branch[1]) < std::make_pair(f.branch[0], f.branch[1]);
			}

			JunctionStorage::JunctionSequentialIterator branch[2];
		};

		int64_t ChainLength(const Fork & now, const Fork & next) const
		{
			return min(abs(now.branch[0].GetPosition() - next.branch[0].GetPosition()), abs(now.branch[1].GetPosition() - next.branch[1].GetPosition()));
		}

		void BubbledBranchesForward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				if ((vertex + 1).Valid())
				{
					parallelEdge[TwoPaCo::DnaChar::MakeUpChar(vertex.GetChar())].push_back(i);
				}

				for (int64_t startPosition = vertex++.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; ++vertex)
				{
					int64_t nowVertexId = vertex.GetVertexId();
					auto point = visit.find(nowVertexId);
					if (point == visit.end())
					{
						BranchData bData;
						bData.branchId.push_back(i);
						visit[nowVertexId] = bData;
					}
					else
					{
						point->second.branchId.push_back(i);
					}
				}
			}

			for (size_t i = 0; i < 5; i++)
			{
				for (size_t j = 0; j < parallelEdge[i].size(); j++)
				{
					for (size_t k = j + 1; k < parallelEdge[i].size(); k++)
					{
						size_t smallBranch = parallelEdge[i][j];
						size_t largeBranch = parallelEdge[i][k];
						bulges[smallBranch].push_back(largeBranch);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		void BubbledBranchesBackward(int64_t vertexId, const std::vector<JunctionStorage::JunctionSequentialIterator> & instance, BubbledBranches & bulges) const
		{
			std::vector<size_t> parallelEdge[5];
			std::map<int64_t, BranchData> visit;
			bulges.assign(instance.size(), std::vector<size_t>());
			for (size_t i = 0; i < instance.size(); i++)
			{
				auto iPrev = instance[i] - 1;
				if (iPrev.Valid())
				{
					for (size_t j = i + 1; j < instance.size(); j++)
					{
						auto jPrev = instance[j] - 1;
						if (jPrev.Valid() && iPrev.GetVertexId() == jPrev.GetVertexId() && iPrev.GetChar() == jPrev.GetChar())
						{
							bulges[i].push_back(j);
						}
					}
				}
			}


			for (size_t i = 0; i < instance.size(); i++)
			{
				auto vertex = instance[i];
				auto prev = vertex - 1;

				for (int64_t startPosition = vertex--.GetPosition(); vertex.Valid() && abs(startPosition - vertex.GetPosition()) <= maxBranchSize_; --vertex)
				{
					int64_t nowVertexId = vertex.GetVertexId();
					auto point = visit.find(nowVertexId);
					if (point == visit.end())
					{
						BranchData bData;
						bData.branchId.push_back(i);
						visit[nowVertexId] = bData;
					}
					else
					{
						point->second.branchId.push_back(i);
					}
				}
			}

			for (auto point = visit.begin(); point != visit.end(); ++point)
			{
				std::sort(point->second.branchId.begin(), point->second.branchId.end());
				for (size_t j = 0; j < point->second.branchId.size(); j++)
				{
					for (size_t k = j + 1; k < point->second.branchId.size(); k++)
					{
						size_t smallBranch = point->second.branchId[j];
						size_t largeBranch = point->second.branchId[k];
						if (smallBranch != largeBranch && std::find(bulges[smallBranch].begin(), bulges[smallBranch].end(), largeBranch) == bulges[smallBranch].end())
						{
							bulges[smallBranch].push_back(largeBranch);
						}
					}
				}
			}
		}

		struct CheckIfSource
		{
		public:
			BlocksFinder & finder;
			std::vector<int64_t> & shuffle;

			CheckIfSource(BlocksFinder & finder, std::vector<int64_t> & shuffle) : finder(finder), shuffle(shuffle)
			{
			}

			void operator()(tbb::blocked_range<size_t> & range) const
			{
				BubbledBranches forwardBubble;
				BubbledBranches backwardBubble;
				std::vector<JunctionStorage::JunctionSequentialIterator> instance;
				for (size_t r = range.begin(); r != range.end(); r++)
				{
					if (finder.count_++ % 10000 == 0)
					{
						tbb::mutex::scoped_lock lock(finder.globalMutex_);
						std::cout << finder.count_ << std::endl;
					}

					instance.clear();
					int64_t vertex = shuffle[r];
					for (auto it = JunctionStorage::JunctionIterator(vertex); it.Valid(); ++it)
					{
						instance.push_back(it.SequentialIterator());
					}

					bool stop = false;
					finder.BubbledBranchesForward(vertex, instance, forwardBubble);
					finder.BubbledBranchesBackward(vertex, instance, backwardBubble);
					for (size_t i = 0; i < forwardBubble.size() && !stop; i++)
					{
						for (size_t j = 0; j < forwardBubble[i].size(); j++)
						{
							size_t k = forwardBubble[i][j];
							auto it = std::find(backwardBubble[i].begin(), backwardBubble[i].end(), k);
							if (it == backwardBubble[i].end())
							{
								tbb::mutex::scoped_lock lock(finder.globalMutex_);
								finder.source_.push_back(vertex);
								stop = true;
								break;
							}
						}
					}

					stop = false;
					for (size_t i = 0; i < backwardBubble.size() && !stop; i++)
					{
						for (size_t j = 0; j < backwardBubble[i].size(); j++)
						{
							size_t k = backwardBubble[i][j];
							if (std::find(forwardBubble[i].begin(), forwardBubble[i].end(), k) == forwardBubble[i].end() && (instance[i].IsPositiveStrand() || instance[k].IsPositiveStrand()))
							{
								tbb::mutex::scoped_lock lock(finder.globalMutex_);
								finder.sink_.push_back(vertex);
								stop = true;
								break;
							}
						}
					}
				}
			}
		};


		int64_t k_;
		size_t progressCount_;
		size_t progressPortion_;
		tbb::mutex globalMutex_;
		std::atomic<int64_t> count_;
		std::atomic<int64_t> blocksFound_;
		int64_t sampleSize_;
		int64_t scalingFactor_;
		bool scoreFullChains_;
		int64_t lookingDepth_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t maxFlankingSize_;
		JunctionStorage & storage_;
		tbb::mutex progressMutex_;
		tbb::mutex blocksMutex_;
		std::ofstream debugOut_;
		std::vector<int64_t> source_;
		std::vector<int64_t> sink_;
		std::vector<BlockInstance> blocksInstance_;
		std::vector<std::vector<Edge> > syntenyPath_;
#ifdef _DEBUG_OUT_
		bool debug_;
		std::set<int64_t> missingVertex_;
#endif
	};
}

#endif
