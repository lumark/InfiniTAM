// Copyright 2014 Isis Innovation Limited and the authors of InfiniTAM

#include "ITMColorTracker.h"
#include "../Utils/ITMCholesky.h"

#include <math.h>

using namespace ITMLib::Engine;

static inline bool minimizeLM(const ITMColorTracker & tracker, ITMPose & initialization);

ITMColorTracker::ITMColorTracker(Vector2i imgSize, int noHierarchyLevels, int noRotationOnlyLevels,
	ITMLowLevelEngine *lowLevelEngine, bool useGPU)
{
	viewHierarchy = new ITMImageHierarchy<ITMViewHierarchyLevel>(imgSize, noHierarchyLevels, noRotationOnlyLevels, useGPU);

	this->lowLevelEngine = lowLevelEngine;
}

ITMColorTracker::~ITMColorTracker(void)
{
	delete viewHierarchy;
}

void ITMColorTracker::TrackCamera(ITMTrackingState *trackingState, const ITMView *view)
{
	this->view = view; this->trackingState = trackingState;

	this->PrepareForEvaluation(view);

	ITMPose currentPara(view->calib->trafo_rgb_to_depth.calib_inv * trackingState->pose_d->M);
	for (int levelId = viewHierarchy->noLevels - 1; levelId >= 0; levelId--)
	{
		this->levelId = levelId;
		this->rotationOnly = viewHierarchy->levels[levelId]->rotationOnly;

		minimizeLM(*this, currentPara);
	}

	// these following will coerce the result back into the chosen
	// parameterization for rotations
	trackingState->pose_d->SetFrom(view->calib->trafo_rgb_to_depth.calib * currentPara.M);

	//printf(">> %f %f %f %f %f %f\n", scene->pose->params.each.rx, scene->pose->params.each.ry, scene->pose->params.each.rz,
	//	scene->pose->params.each.tx, scene->pose->params.each.ty, scene->pose->params.each.tz);
}

void ITMColorTracker::PrepareForEvaluation(const ITMView *view)
{
	lowLevelEngine->CopyImage(viewHierarchy->levels[0]->rgb, view->rgb);

	ITMImageHierarchy<ITMViewHierarchyLevel> *hierarchy = viewHierarchy;

	for (int i = 1; i < hierarchy->noLevels; i++)
	{
		ITMViewHierarchyLevel *currentLevel = hierarchy->levels[i], *previousLevel = hierarchy->levels[i - 1];
		lowLevelEngine->FilterSubsample(currentLevel->rgb, previousLevel->rgb);
	}

	for (int i = 0; i < hierarchy->noLevels; i++)
	{
		ITMViewHierarchyLevel *currentLevel = hierarchy->levels[i];

		lowLevelEngine->GradientX(currentLevel->gradientX_rgb, currentLevel->rgb);
		lowLevelEngine->GradientY(currentLevel->gradientY_rgb, currentLevel->rgb);
	}
}

void ITMColorTracker::applyDelta(const ITMPose & para_old, const float *delta, ITMPose & para_new) const
{
	float paramVector[6];

	if (rotationOnly)
	{
		paramVector[0] = 0.0f; paramVector[1] = 0.0f; paramVector[2] = 0.0f;
		paramVector[3] = (float)(delta[0]); paramVector[4] = (float)(delta[1]); paramVector[5] = (float)(delta[2]);
	}
	else
	{
		paramVector[0] = (float)(delta[0]); paramVector[1] = (float)(delta[1]); paramVector[2] = (float)(delta[2]);
		paramVector[3] = (float)(delta[3]); paramVector[4] = (float)(delta[4]); paramVector[5] = (float)(delta[5]);
	}
	para_new.SetFrom(paramVector); para_new.SetModelViewFromParams();
	para_new.MultiplyWith(&(para_old));
}

void ITMColorTracker::EvaluationPoint::computeGradients(bool hessianRequired)
{
	int numPara = mParent->numParameters();
	cacheNabla = new float[numPara];
	cacheHessian = new float[numPara*numPara];

	mParent->G_oneLevel(cacheNabla, cacheHessian, mPara);
}

ITMColorTracker::EvaluationPoint::EvaluationPoint(ITMPose *pos, const ITMColorTracker *f_parent)
{
	float localF[1];

	this->mPara = pos; this->mParent = f_parent;

	ITMColorTracker *parent = (ITMColorTracker *)mParent;

	parent->F_oneLevel(localF, mPara);

	cacheF = localF[0];
	cacheHessian = NULL; cacheNabla = NULL;
}

// LM optimisation

static inline double stepQuality(ITMColorTracker::EvaluationPoint *x, ITMColorTracker::EvaluationPoint *x2, const float *step, const float *grad, const float *B, int numPara)
{
	double actual_reduction = x->f() - x2->f();
	double predicted_reduction = 0.0;
	float *tmp = new float[numPara];

	matmul(B, step, tmp, numPara, numPara);
	for (int i = 0; i < numPara; i++) predicted_reduction -= grad[i] * step[i] + 0.5*step[i] * tmp[i];
	delete[] tmp;

	if (predicted_reduction < 0) return actual_reduction / fabs(predicted_reduction);
	return actual_reduction / predicted_reduction;
}

static inline bool minimizeLM(const ITMColorTracker & tracker, ITMPose & initialization)
{
	// These are some sensible default parameters for Levenberg Marquardt.
	// The first three control the convergence criteria, the others might
	// impact convergence speed.
	static const int MAX_STEPS = 100;
	static const float MIN_STEP = 0.00005f;
	static const float MIN_DECREASE = 0.00001f;
	static const float TR_QUALITY_GAMMA1 = 0.75f;
	static const float TR_QUALITY_GAMMA2 = 0.25f;
	static const float TR_REGION_INCREASE = 2.0f;
	static const float TR_REGION_DECREASE = 0.25f;

	int numPara = tracker.numParameters();
	float *d = new float[numPara];
	float lambda = 0.01f;
	int step_counter = 0;

	ITMColorTracker::EvaluationPoint *x = tracker.evaluateAt(new ITMPose(initialization));
	ITMColorTracker::EvaluationPoint *x2 = NULL;

	if (!portable_finite(x->f())) { delete[] d; delete x; return false; }

	do
	{
		const float *grad;
		const float *B;

		grad = x->nabla_f();
		B = x->hessian_GN();

		bool success;
		{
			float *A = new float[numPara*numPara];
			for (int i = 0; i < numPara*numPara; ++i) A[i] = B[i];
			for (int i = 0; i < numPara; ++i)
			{
				float & ele = A[i*(numPara + 1)];
				if (!(fabs(ele) < 1e-15f)) ele *= (1.0f + lambda); else ele = lambda*1e-10f;
			}

			ITMLib::Utils::ITMCholesky cholA(A, numPara);

			cholA.Backsub(&(d[0]), grad);
			// TODO: if Cholesky failed, set success to false!

			success = true;
			delete[] A;
		}

		if (success)
		{
			float MAXnorm = 0.0;
			for (int i = 0; i<numPara; i++) { float tmp = fabs(d[i]); if (tmp>MAXnorm) MAXnorm = tmp; }

			if (MAXnorm < MIN_STEP) break;
			for (int i = 0; i < numPara; i++) d[i] = -d[i];

			// make step
			ITMPose *tmp_para = new ITMPose(x->getParameter());
			tracker.applyDelta(x->getParameter(), &(d[0]), *tmp_para);

			// check whether step reduces error function and
			// compute a new value of lambda
			x2 = tracker.evaluateAt(tmp_para);

			double rho = stepQuality(x, x2, &(d[0]), grad, B, numPara);
			if (rho > TR_QUALITY_GAMMA1) lambda = lambda / TR_REGION_INCREASE;
			else if (rho <= TR_QUALITY_GAMMA2) { success = false; lambda = lambda / TR_REGION_DECREASE; }
		}
		else
		{
			x2 = NULL;
			// can't compute a step quality here...
			lambda = lambda / TR_REGION_DECREASE;
		}

		if (success)
		{
			// accept step
			bool continueIteration = true;
			if (!(x2->f() < (x->f() - fabs(x->f()) * MIN_DECREASE))) continueIteration = false;

			delete x;
			x = x2;

			if (!continueIteration) break;
		}
		else if (x2 != NULL) delete x2;
		if (step_counter++ >= MAX_STEPS - 1) break;
	} while (true);

	initialization.SetFrom(&(x->getParameter()));
	delete x;

	delete[] d;

	return true;
}
