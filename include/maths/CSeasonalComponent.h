/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */

#ifndef INCLUDED_ml_maths_CSeasonalComponent_h
#define INCLUDED_ml_maths_CSeasonalComponent_h

#include <core/CMemory.h>
#include <core/CoreTypes.h>

#include <maths/CPRNG.h>
#include <maths/CSeasonalComponentAdaptiveBucketing.h>
#include <maths/CDecompositionComponent.h>
#include <maths/ImportExport.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ml
{
namespace core
{
class CStatePersistInserter;
class CStateRestoreTraverser;
}
namespace maths
{

//! \brief Estimates a seasonal component of a time series.
//!
//! DESCRIPTION:\n
//! This uses an adaptive bucketing strategy to compute a linear (in time) regression
//! through and variance of a periodic function in various subintervals of its period.
//!
//! The intervals are adjusted to minimize the maximum averaging error in any bucket (see
//! CSeasonalComponentAdaptiveBucketing for more details). Estimates of the true function
//! values are obtained by interpolating the bucket values (using cubic spline).
//!
//! The bucketing is aged by relaxing it back towards uniform and aging the counts of the
//! mean value for each bucket as usual.
class MATHS_EXPORT CSeasonalComponent : private CDecompositionComponent
{
    public:
        typedef CBasicStatistics::SSampleMeanVar<double>::TAccumulator TMeanVarAccumulator;
        typedef std::pair<core_t::TTime, core_t::TTime> TTimeTimePr;
        typedef std::pair<TTimeTimePr, TMeanVarAccumulator> TTimeTimePrMeanVarPr;
        typedef std::vector<TTimeTimePrMeanVarPr> TTimeTimePrMeanVarPrVec;
        typedef CSymmetricMatrixNxN<double, 2> TMatrix;

    public:
        //! \param[in] time The time provider.
        //! \param[in] maxSize The maximum number of component buckets.
        //! \param[in] decayRate Controls the rate at which information is lost from
        //! its adaptive bucketing.
        //! \param[in] minimumBucketLength The minimum bucket length permitted in the
        //! adaptive bucketing.
        //! \param[in] boundaryCondition The boundary condition to use for the splines.
        //! \param[in] valueInterpolationType The style of interpolation to use for
        //! computing values.
        //! \param[in] varianceInterpolationType The style of interpolation to use for
        //! computing variances.
        CSeasonalComponent(const CSeasonalTime &time,
                           std::size_t maxSize,
                           double decayRate = 0.0,
                           double minimumBucketLength = 0.0,
                           CSplineTypes::EBoundaryCondition boundaryCondition = CSplineTypes::E_Periodic,
                           CSplineTypes::EType valueInterpolationType = CSplineTypes::E_Cubic,
                           CSplineTypes::EType varianceInterpolationType = CSplineTypes::E_Linear);

        //! Construct by traversing part of an state document.
        CSeasonalComponent(double decayRate,
                           double minimumBucketLength,
                           core::CStateRestoreTraverser &traverser,
                           CSplineTypes::EType valueInterpolationType = CSplineTypes::E_Cubic,
                           CSplineTypes::EType varianceInterpolationType = CSplineTypes::E_Linear);

        //! An efficient swap of the contents of two components.
        void swap(CSeasonalComponent &other);

        //! Persist state by passing information to \p inserter.
        void acceptPersistInserter(core::CStatePersistInserter &inserter) const;

        //! Check if the seasonal component has been estimated.
        bool initialized(void) const;

        //! Initialize the adaptive bucketing.
        bool initialize(core_t::TTime startTime,
                        core_t::TTime endTime,
                        const TTimeTimePrMeanVarPrVec &values);

        //! Get the size of this component.
        std::size_t size(void) const;

        //! Clear all data.
        void clear(void);

        //! Shift the component's time origin to \p time.
        void shiftOrigin(core_t::TTime time);

        //! Shift the component's values by \p shift.
        void shiftLevel(double shift);

        //! Shift the component's slope by \p shift.
        void shiftSlope(double shift);

        //! Adds a value \f$(t, f(t))\f$ to this component.
        //!
        //! \param[in] time The time of the point.
        //! \param[in] value The value at \p time.
        //! \param[in] weight The weight of \p value. The smaller this is the
        //! less influence it has on the component.
        void add(core_t::TTime time, double value, double weight = 1.0);

        //! Update the interpolation of the bucket values.
        //!
        //! \param[in] time The time at which to interpolate.
        //! \param[in] refine If false disable refining the bucketing.
        void interpolate(core_t::TTime time, bool refine = true);

        //! Get the rate at which the seasonal component loses information.
        double decayRate(void) const;

        //! Set the rate at which the seasonal component loses information.
        void decayRate(double decayRate);

        //! Age out old data to account for elapsed \p time.
        void propagateForwardsByTime(double time, bool meanRevert = false);

        //! Get the time provider.
        const CSeasonalTime &time(void) const;

        //! Interpolate the component at \p time.
        //!
        //! \param[in] time The time of interest.
        //! \param[in] confidence The symmetric confidence interval for the variance
        //! as a percentage.
        TDoubleDoublePr value(core_t::TTime time, double confidence) const;

        //! Get the mean value of the component.
        double meanValue(void) const;

        //! Get the difference from the mean of repeats at \p period.
        //!
        //! This computes the quantity:
        //! <pre class="fragment">
        //!    \f$\sum_i f(t + p i)\f$
        //! </pre>
        //!
        //! where \f$t\f$ is \p time and \f$p\f$ is \p period and must divide
        //! this component's period \f$P\f$. The sum ranges over \f$[P/p]\f$.
        double differenceFromMean(core_t::TTime time, core_t::TTime period) const;

        //! Get the variance of the residual about the prediction at \p time.
        //!
        //! \param[in] time The time of interest.
        //! \param[in] confidence The symmetric confidence interval for the
        //! variance as a percentage.
        TDoubleDoublePr variance(core_t::TTime time, double confidence) const;

        //! Get the mean variance of the component residuals.
        double meanVariance(void) const;

        //! Get the maximum ratio between a residual variance and the mean
        //! residual variance.
        double heteroscedasticity(void) const;

        //! Get the variance in the prediction due to drift in the regression
        //! model parameters expected by \p time.
        double varianceDueToParameterDrift(core_t::TTime time) const;

        //! Get the covariance matrix of the regression parameters' at \p time.
        //!
        //! \param[in] time The time of interest.
        //! \param[out] result Filled in with the regression parameters'
        //! covariance matrix.
        bool covariances(core_t::TTime time, TMatrix &result) const;

        //! Get the value spline.
        TSplineCRef valueSpline(void) const;

        //! Get the common slope of the bucket regression models.
        double slope(void) const;

        //! Check if the bucket regression models have enough history to predict.
        bool sufficientHistoryToPredict(core_t::TTime time) const;

        //! Get a checksum for this object.
        uint64_t checksum(uint64_t seed = 0) const;

        //! Debug the memory used by this component.
        void debugMemoryUsage(core::CMemoryUsage::TMemoryUsagePtr mem) const;

        //! Get the memory used by this component.
        std::size_t memoryUsage(void) const;

    private:
        //! Create by traversing a state document.
        bool acceptRestoreTraverser(double decayRate,
                                    double minimumBucketLength,
                                    core::CStateRestoreTraverser &traverser);

        //! Get a jitter to apply to the prediction time.
        core_t::TTime jitter(core_t::TTime time);

    private:
        //! Used to apply jitter to added value times so that we can accommodate
        //! small time translations of the trend.
        CPRNG::CXorOShiro128Plus m_Rng;

        //! Regression models for a collection of buckets covering the period.
        CSeasonalComponentAdaptiveBucketing m_Bucketing;
};

//! Create a free function which will be picked up in Koenig lookup.
inline void swap(CSeasonalComponent &lhs, CSeasonalComponent &rhs)
{
    lhs.swap(rhs);
}

}
}

#endif // INCLUDED_ml_maths_CSeasonalComponent_h