#ifndef _PATH_H_
#define _PATH_H_

#include <cassert>
#include "distancekeeper.h"

namespace Sibelia
{
	struct Assignment
	{
		static const int64_t IN_USE;
		static const int64_t UNKNOWN_BLOCK;
		int64_t block;
		int64_t instance;
		Assignment() : block(UNKNOWN_BLOCK), instance(UNKNOWN_BLOCK)
		{

		}

		bool operator == (const Assignment & assignment) const
		{
			return block == assignment.block && instance == assignment.instance;
		}
	};

	struct BestPath;

	struct Path
	{
	public:
		Path(const JunctionStorage & storage,			
			int64_t maxBranchSize,
			int64_t minBlockSize,
			int64_t maxFlankingSize,
			std::vector<std::vector<Assignment> > & blockId,
			bool checkConsistency = false) :
			maxBranchSize_(maxBranchSize), minBlockSize_(minBlockSize), maxFlankingSize_(maxFlankingSize), storage_(&storage),
			distanceKeeper_(storage.GetVerticesNumber()), minChainSize_(minBlockSize - 2 * maxFlankingSize), blockId_(blockId),
			checkConsistency_(checkConsistency)
		{
			
		}

		void Init(int64_t vid)
		{
			origin_ = vid;			
			distanceKeeper_.Set(vid, 0);
			for (size_t i = 0; i < storage_->GetInstancesCount(vid); i++)
			{
				JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vid, i);
				if (blockId_[it.GetChrId()][it.GetIndex()].block == Assignment::UNKNOWN_BLOCK)
				{
					instance_.push_back(Instance(it));
				}
			}
		}

		void Clean()
		{
			distanceKeeper_.Unset(origin_);
			for (auto & inst : instance_)
			{
				JunctionStorage::JunctionIterator start = inst.Front();
				JunctionStorage::JunctionIterator end = inst.Back();
				do
				{
					auto & bid = blockId_[start.GetChrId()][start.GetIndex()].block;
					if (bid == Assignment::IN_USE)
					{
						bid = Assignment::UNKNOWN_BLOCK;
					}

				} while (start++ != end);
			}

			for (auto & p : leftBody_)
			{
				distanceKeeper_.Unset(p.Edge().GetEndVertex());
				distanceKeeper_.Unset(p.Edge().GetStartVertex());
			}

			for (auto & p : rightBody_)
			{
				distanceKeeper_.Unset(p.Edge().GetEndVertex());
				distanceKeeper_.Unset(p.Edge().GetStartVertex());
			}

			leftBody_.clear();
			rightBody_.clear();
			instance_.clear();
		}

		struct Instance
		{	
		private:
			JunctionStorage::JunctionIterator front_;
			JunctionStorage::JunctionIterator back_;
		public:			
		
			Instance()
			{

			}

			Instance(JunctionStorage::JunctionIterator & it) : front_(it), back_(it)
			{

			}

			void ChangeFront(const JunctionStorage::JunctionIterator & it)
			{
				front_ = it;
			}

			void ChangeBack(const JunctionStorage::JunctionIterator & it)
			{
				back_ = it;
			}			

			bool SinglePoint() const
			{
				return front_ == back_;
			}

			JunctionStorage::JunctionIterator Front() const
			{
				return front_;
			}

			JunctionStorage::JunctionIterator Back() const
			{
				return back_;
			}

			int64_t LeftFlankDistance(const DistanceKeeper & keeper, const JunctionStorage * storage_) const
			{
				return keeper.Get(front_.GetVertexId(storage_));
			}

			int64_t RightFlankDistance(const DistanceKeeper & keeper, const JunctionStorage * storage_) const
			{
				return keeper.Get(back_.GetVertexId(storage_));
			}
		};

		struct Point
		{
		private:
			Edge edge;
			int64_t startDistance;
		public:
			Point() {}
			Point(Edge edge, int64_t startDistance) : edge(edge), startDistance(startDistance) {}

			Edge Edge() const
			{
				return edge;
			}

			int64_t StartDistance() const
			{
				return startDistance;
			}

			int64_t EndDistance() const
			{
				return startDistance + edge.GetLength();
			}

			bool operator == (const Point & p) const
			{
				return startDistance == p.startDistance && edge == p.edge;
			}

			bool operator != (const Point & p) const
			{
				return p != *this;
			}
		};

		int64_t Origin() const
		{
			return origin_;
		}

		/*
		void PrintInstance(const Instance & inst, std::ostream & out) const
		{
			for (auto & it : inst.seq)
			{
				out << "{vid:" << it.GetVertexId()
					<< " str:" << inst.seq.front().IsPositiveStrand()
					<< " chr:" << inst.seq.front().GetChrId()
					<< " pos:" << it.GetPosition() << "} ";
			}

			out << std::endl;
		}
		
		void DebugOut(std::ostream & out, bool all = true) const
		{
			out << "Path: ";
			for (auto it = leftBody_.rbegin(); it != leftBody_.rend(); ++it)
			{
				auto & pt = *it;
				out << "(" << pt.edge.GetStartVertex() << "," << pt.edge.GetEndVertex() << "," << pt.edge.GetChar() << ") ";
			}

			for (auto it = rightBody_.rbegin(); it != rightBody_.rend(); ++it)
			{
				auto & pt = *it;
				out << "(" << pt.edge.GetStartVertex() << "," << pt.edge.GetEndVertex() << "," << pt.edge.GetChar() << ") ";
			}

			out << std::endl << "Instances: " << std::endl;
			for (auto & inst : instance_)
			{
				if (all || IsGoodInstance(inst))
				{
					PrintInstance(inst, out);
				}
			}

			out << std::endl;
		}*/

		const std::vector<Instance> & Instances() const
		{
			return instance_;
		}

		int64_t MiddlePathLength() const
		{
			return (rightBody_.size() > 0 ? rightBody_.back().EndDistance() : 0) - (leftBody_.size() > 0 ? leftBody_.back().StartDistance() : 0);
		}

		int64_t GetEndVertex() const
		{
			if (rightBody_.size() > 0)
			{
				return rightBody_.back().Edge().GetEndVertex();
			}

			return origin_;
		}

		int64_t GetStartVertex() const
		{
			if (leftBody_.size() > 0)
			{
				return leftBody_.back().Edge().GetStartVertex();
			}

			return origin_;
		}

		void DumpPath(std::vector<Edge> & ret) const
		{
			ret.clear();
			for (auto it = leftBody_.rbegin(); it != leftBody_.rend(); ++it)
			{
				ret.push_back(it->Edge());
			}

			for (auto it = rightBody_.rbegin(); it != rightBody_.rend(); ++it)
			{
				ret.push_back(it->Edge());
			}
		}		

		bool PointPushBack(const Edge & e)
		{
			int64_t vertex = e.GetEndVertex();
			if (distanceKeeper_.IsSet(vertex))
			{
				return false;
			}

			int64_t startVertexDistance = rightBody_.empty() ? 0 : rightBody_.back().EndDistance();
			int64_t endVertexDistance = startVertexDistance + e.GetLength();
			distanceKeeper_.Set(e.GetEndVertex(), endVertexDistance);
			for (auto & inst : instance_)
			{
				int64_t chrId = inst.Back().GetChrId();
				JunctionStorage::JunctionIterator startIt = inst.Back();
				JunctionStorage::JunctionIterator nowIt = startIt + 1;
				if (nowIt.Valid(storage_) && blockId_[chrId][nowIt.GetIndex()].block == Assignment::UNKNOWN_BLOCK)
				{
					bool reach = false;
					if (startIt.GetVertexId(storage_) == e.GetStartVertex() && nowIt.GetVertexId(storage_) == vertex && inst.Back().GetChar(storage_) == e.GetChar())
					{
						reach = true;
					}
					else
					{
						if (abs(endVertexDistance - inst.RightFlankDistance(distanceKeeper_, storage_)) <= maxBranchSize_)
						{
							for (; nowIt.Valid(storage_) && blockId_[chrId][nowIt.GetIndex()].block == Assignment::UNKNOWN_BLOCK && abs(nowIt.GetPosition(storage_) - startIt.GetPosition(storage_)) <= maxBranchSize_; ++nowIt)
							{
								if (nowIt.GetVertexId(storage_) == vertex)
								{
									reach = true;
									break;
								}
							}
						}
					}

					if (reach)
					{
						int64_t nextLength = abs(nowIt.GetPosition(storage_) - inst.Front().GetPosition(storage_));
						int64_t leftFlankSize = abs(inst.LeftFlankDistance(distanceKeeper_, storage_) - (leftBody_.empty() ? 0 : leftBody_.back().StartDistance()));
						if (nextLength >= minChainSize_ && leftFlankSize > maxFlankingSize_)
						{
							rightBody_.push_back(Point(e, startVertexDistance));
							distanceKeeper_.Set(e.GetEndVertex(), endVertexDistance);
							PointPopBack();
							return false;
						}

						blockId_[nowIt.GetChrId()][nowIt.GetIndex()].block = Assignment::IN_USE;						
						inst.ChangeBack(nowIt);						
					}
				}
			}


			for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
			{
				JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
				if (blockId_[it.GetChrId()][it.GetIndex()].block == Assignment::UNKNOWN_BLOCK)
				{
					instance_.push_back(Instance(it));
					blockId_[it.GetChrId()][it.GetIndex()].block = Assignment::IN_USE;
				}
			}

			rightBody_.push_back(Point(e, startVertexDistance));			
			return true;
		}

		void PointPopBack()
		{
			int64_t lastVertex = rightBody_.back().Edge().GetEndVertex();
			rightBody_.pop_back();
			distanceKeeper_.Unset(lastVertex);
			for (auto it = instance_.rbegin(); it != instance_.rend(); )
			{
				if (it->Back().GetVertexId(storage_) == lastVertex)
				{
					blockId_[it->Back().GetChrId()][it->Back().GetIndex()].block = Assignment::UNKNOWN_BLOCK;
					if (it->Front() == it->Back())
					{						
						assert(it == instance_.rbegin());
						instance_.pop_back();
						it = instance_.rbegin();
					}
					else
					{
						auto jt = it->Back();
						while (true)
						{
							if (distanceKeeper_.IsSet(jt.GetVertexId(storage_)))
							{
								it->ChangeBack(jt);
								break;
							}
							else
							{
								--jt;
							}
						}

						it++;
					}				
				}
				else
				{
					++it;
				}
			}
		}

		bool PointPushFront(const Edge & e)
		{		
			int64_t vertex = e.GetStartVertex();
			if (distanceKeeper_.IsSet(vertex))
			{
				return false;
			}

			int64_t endVertexDistance = leftBody_.empty() ? 0 : leftBody_.back().StartDistance();
			int64_t startVertexDistance = endVertexDistance - e.GetLength();
			
			distanceKeeper_.Set(e.GetStartVertex(), startVertexDistance);
			for (auto & inst : instance_)
			{
				int64_t chrId = inst.Back().GetChrId();
				JunctionStorage::JunctionIterator startIt = inst.Front();
				JunctionStorage::JunctionIterator nowIt = startIt - 1;
				if (nowIt.Valid(storage_) && blockId_[chrId][nowIt.GetIndex()].block == Assignment::UNKNOWN_BLOCK)
				{
					bool reach = false;
					if (nowIt.GetVertexId(storage_) == vertex && startIt.GetVertexId(storage_) == e.GetEndVertex() && nowIt.GetChar(storage_) == e.GetChar())
					{
						reach = true;
					}
					else
					{
						if (abs(endVertexDistance - inst.LeftFlankDistance(distanceKeeper_, storage_)) <= maxBranchSize_)
						{
							for (; nowIt.Valid(storage_) && blockId_[chrId][nowIt.GetIndex()].block == Assignment::UNKNOWN_BLOCK && abs(nowIt.GetPosition(storage_) - startIt.GetPosition(storage_)) <= maxBranchSize_; --nowIt)
							{
								if (nowIt.GetVertexId(storage_) == vertex)
								{
									reach = true;
									break;
								}
							}
						}
					}

					if (reach)
					{
						int64_t nextLength = abs(nowIt.GetPosition(storage_) - inst.Back().GetPosition(storage_));
						int64_t rightFlankSize = abs(inst.RightFlankDistance(distanceKeeper_, storage_) - (rightBody_.empty() ? 0 : rightBody_.back().EndDistance()));
						if (nextLength >= minChainSize_ && rightFlankSize > maxFlankingSize_)
						{
							leftBody_.push_back(Point(e, startVertexDistance));							
							PointPopFront();
							return false;
						}

						blockId_[nowIt.GetChrId()][nowIt.GetIndex()].block = Assignment::IN_USE;
						inst.ChangeFront(nowIt);
					}
				}
			}


			for (size_t i = 0; i < storage_->GetInstancesCount(vertex); i++)
			{
				JunctionStorage::JunctionIterator it = storage_->GetJunctionInstance(vertex, i);
				if (blockId_[it.GetChrId()][it.GetIndex()].block == Assignment::UNKNOWN_BLOCK)
				{
					instance_.push_back(Instance(it));
					blockId_[it.GetChrId()][it.GetIndex()].block = Assignment::IN_USE;
				}
			}

			leftBody_.push_back(Point(e, startVertexDistance));
			return true;
		}

		void PointPopFront()
		{
			int64_t lastVertex = leftBody_.back().Edge().GetStartVertex();
			leftBody_.pop_back();
			distanceKeeper_.Unset(lastVertex);
			for (auto it = instance_.rbegin(); it != instance_.rend(); )
			{
				if (it->Front().GetVertexId(storage_) == lastVertex)
				{
					blockId_[it->Front().GetChrId()][it->Front().GetIndex()].block = Assignment::UNKNOWN_BLOCK;
					if (it->Front() == it->Back())
					{
						assert(it == instance_.rbegin());
						instance_.pop_back();
						it = instance_.rbegin();
					}
					else
					{
						auto jt = it->Front();
						while (true)
						{
							if (distanceKeeper_.IsSet(jt.GetVertexId(storage_)))
							{
								it->ChangeFront(jt);
								break;
							}
							else
							{
								++jt;
							}
						}

						it++;
					}
				}
				else
				{
					++it;
				}
			}
		}

		int64_t Score(bool final = false) const
		{
			int64_t score;
			int64_t length;
			int64_t ret = 0;
			for (auto & inst : instance_)
			{
				InstanceScore(inst, length, score);
				if (!final || length >= minChainSize_)
				{
					ret += score;
				}
			}

			return ret;
		}

		int64_t GoodInstances() const
		{
			int64_t ret = 0;
			for (auto & it : instance_)
			{
				if (IsGoodInstance(it))
				{
					ret++;
				}
			}

			return ret;
		}

		bool IsGoodInstance(const Instance & it) const
		{
			int64_t score;
			int64_t length;
			InstanceScore(it, length, score);
			return length >= minChainSize_;
		}

		void InstanceScore(const Instance & inst, int64_t & length, int64_t & score) const
		{
			int64_t leftFlank = abs(inst.LeftFlankDistance(distanceKeeper_, storage_) - (leftBody_.size() > 0 ? leftBody_.back().StartDistance() : 0));
			int64_t rightFlank = abs(inst.RightFlankDistance(distanceKeeper_, storage_) - (rightBody_.size() > 0 ? rightBody_.back().EndDistance() : 0));
			length = abs(inst.Front().GetPosition(storage_) - inst.Back().GetPosition(storage_));
			score = length - leftFlank - rightFlank;
		}

	private:


		friend class BestPath;

		std::vector<Point> leftBody_;
		std::vector<Point> rightBody_;
		std::vector<Instance> instance_;

		int64_t origin_;
		int64_t minChainSize_;
		int64_t minBlockSize_;
		int64_t maxBranchSize_;
		int64_t maxFlankingSize_;
		bool checkConsistency_;
		DistanceKeeper distanceKeeper_;
		const JunctionStorage * storage_;		
		std::vector<std::vector<Assignment> > & blockId_;		
	};

	struct BestPath
	{
		int64_t score_;
		int64_t leftFlank_;
		int64_t rightFlank_;
		std::vector<Path::Point> newLeftBody_;
		std::vector<Path::Point> newRightBody_;

		void FixForward(Path & path)
		{
			for (auto & pt : newRightBody_)
			{
				bool ret = path.PointPushBack(pt.Edge());
				assert(ret);
			}

			newRightBody_.clear();
			rightFlank_ = path.rightBody_.size();
		}

		void FixBackward(Path & path)
		{
			for (auto & pt : newLeftBody_)
			{
				bool ret = path.PointPushFront(pt.Edge());
				assert(ret);
			}

			newLeftBody_.clear();
			leftFlank_ = path.leftBody_.size();
		}

		void UpdateForward(const Path & path, int64_t newScore)
		{
			score_ = newScore;
			newRightBody_.clear();
			std::copy(path.rightBody_.begin() + rightFlank_, path.rightBody_.end(), std::back_inserter(newRightBody_));
		}

		void UpdateBackward(const Path & path, int64_t newScore)
		{
			score_ = newScore;
			newLeftBody_.clear();
			std::copy(path.leftBody_.begin() + leftFlank_, path.leftBody_.end(), std::back_inserter(newLeftBody_));
		}

		BestPath() 
		{
			Init();
		}

		void Init()
		{
			score_ = leftFlank_ = rightFlank_ = 0;
		}
	};
}

#endif

