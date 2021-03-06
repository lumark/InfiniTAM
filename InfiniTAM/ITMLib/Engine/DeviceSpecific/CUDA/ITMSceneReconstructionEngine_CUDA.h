// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../../ITMSceneReconstructionEngine.h"

namespace ITMLib
{
	namespace Engine
	{
		template<class TVoxel, class TIndex>
		class ITMSceneReconstructionEngine_CUDA : public ITMSceneReconstructionEngine<TVoxel,TIndex>
		{};

		template<class TVoxel>
		class ITMSceneReconstructionEngine_CUDA<TVoxel,ITMVoxelBlockHash> : public ITMSceneReconstructionEngine<TVoxel,ITMVoxelBlockHash>
		{
		private:
			uint *noTotalPoints_device;
			int *noAllocatedVoxelEntries_device, *noAllocatedExcessEntries_device, *noLiveEntries_device;
			unsigned char *entriesAllocType_device;
			Vector3s *blockCoords_device;

		public:
			void AllocateSceneFromDepth(ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, const ITMPose *pose);
			
			void IntegrateIntoScene(ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, const ITMPose *pose);
			void CreatePointCloud(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, ITMTrackingState *trackingState, bool skipPoints);

			void CreateICPMaps(const ITMScene<TVoxel,ITMVoxelBlockHash> *scene, const ITMView *view, ITMTrackingState *trackingState);

			ITMSceneReconstructionEngine_CUDA(void);
			~ITMSceneReconstructionEngine_CUDA(void);
		};

		template<class TVoxel>
		class ITMSceneReconstructionEngine_CUDA<TVoxel,ITMPlainVoxelArray> : public ITMSceneReconstructionEngine<TVoxel,ITMPlainVoxelArray>
		{
		private:
			uint *noTotalPoints_device;

		public:
			void AllocateSceneFromDepth(ITMScene<TVoxel,ITMPlainVoxelArray> *scene, const ITMView *view, const ITMPose *pose);
			
			void IntegrateIntoScene(ITMScene<TVoxel,ITMPlainVoxelArray> *scene, const ITMView *view, const ITMPose *pose);
			void CreatePointCloud(const ITMScene<TVoxel,ITMPlainVoxelArray> *scene, const ITMView *view, ITMTrackingState *trackingState, bool skipPoints);

			void CreateICPMaps(const ITMScene<TVoxel,ITMPlainVoxelArray> *scene, const ITMView *view, ITMTrackingState *trackingState);

			ITMSceneReconstructionEngine_CUDA(void);
			~ITMSceneReconstructionEngine_CUDA(void);
		};
	}
}
