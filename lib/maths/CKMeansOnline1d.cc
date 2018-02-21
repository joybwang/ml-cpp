/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */

#include <maths/CKMeansOnline1d.h>

#include <core/CMemory.h>
#include <core/Constants.h>
#include <core/CSmallVector.h>
#include <core/CStatePersistInserter.h>
#include <core/CStateRestoreTraverser.h>

#include <maths/CChecksum.h>
#include <maths/CRestoreParams.h>
#include <maths/CNormalMeanPrecConjugate.h>
#include <maths/MathsTypes.h>

#include <boost/bind.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ml
{
namespace maths
{

namespace
{

typedef core::CSmallVector<double, 1> TDouble1Vec;
typedef core::CSmallVector<double, 4> TDouble4Vec;
typedef core::CSmallVector<TDouble4Vec, 1> TDouble4Vec1Vec;
typedef std::pair<double, double> TDoubleDoublePr;
typedef std::vector<TDoubleDoublePr> TDoubleDoublePrVec;

namespace detail
{

//! \brief Orders two normals by their means.
struct SNormalMeanLess
{
    public:
        bool operator()(const CNormalMeanPrecConjugate &lhs,
                        const CNormalMeanPrecConjugate &rhs) const
        {
            return lhs.marginalLikelihoodMean() < rhs.marginalLikelihoodMean();
        }
        bool operator()(double lhs,
                        const CNormalMeanPrecConjugate &rhs) const
        {
            return lhs < rhs.marginalLikelihoodMean();
        }
        bool operator()(const CNormalMeanPrecConjugate &lhs,
                        double rhs) const
        {
            return lhs.marginalLikelihoodMean() < rhs;
        }
};

//! Get the log of the likelihood that \p point is from \p normal.
double logLikelihoodFromCluster(const TDouble1Vec &sample,
                                const CNormalMeanPrecConjugate &normal)
{
    double likelihood;
    maths_t::EFloatingPointErrorStatus status =
        normal.jointLogMarginalLikelihood(CConstantWeights::COUNT,
                                          sample,
                                          CConstantWeights::SINGLE_UNIT,
                                          likelihood);
    if (status & maths_t::E_FpFailed)
    {
        LOG_ERROR("Unable to compute probability for: " << sample[0]);
        return core::constants::LOG_MIN_DOUBLE - 1.0;
    }
    if (status & maths_t::E_FpOverflowed)
    {
        return likelihood;
    }
    return likelihood + ::log(normal.numberSamples());
}

} // detail::

// 1 - "smallest hard assignment weight"
const double HARD_ASSIGNMENT_THRESHOLD = 0.01;

const std::string CLUSTER_TAG("a");

}

CKMeansOnline1d::CKMeansOnline1d(TNormalVec &clusters)
{
    std::sort(clusters.begin(), clusters.end(), detail::SNormalMeanLess());
    m_Clusters.assign(clusters.begin(), clusters.end());
}

CKMeansOnline1d::CKMeansOnline1d(const SDistributionRestoreParams &params,
                                 core::CStateRestoreTraverser &traverser)
{
    traverser.traverseSubLevel(boost::bind(&CKMeansOnline1d::acceptRestoreTraverser,
                               this, boost::cref(params), _1));
}

bool CKMeansOnline1d::acceptRestoreTraverser(const SDistributionRestoreParams &params,
                                             core::CStateRestoreTraverser &traverser)
{
    do
    {
        const std::string &name = traverser.name();
        if (name == CLUSTER_TAG)
        {
            CNormalMeanPrecConjugate cluster(params, traverser);
            m_Clusters.push_back(cluster);
        }
    }
    while (traverser.next());

    return true;
}

std::string CKMeansOnline1d::persistenceTag(void) const
{
    return K_MEANS_ONLINE_1D_TAG;
}

void CKMeansOnline1d::acceptPersistInserter(core::CStatePersistInserter &inserter) const
{
    for (std::size_t i = 0u; i < m_Clusters.size(); ++i)
    {
        inserter.insertLevel(CLUSTER_TAG, boost::bind(&CNormalMeanPrecConjugate::acceptPersistInserter,
                                                      &m_Clusters[i], _1));
    }
}

CKMeansOnline1d *CKMeansOnline1d::clone(void) const
{
    return new CKMeansOnline1d(*this);
}

void CKMeansOnline1d::clear(void)
{
    m_Clusters.clear();
}

std::size_t CKMeansOnline1d::numberClusters(void) const
{
    return m_Clusters.size();
}

void CKMeansOnline1d::dataType(maths_t::EDataType dataType)
{
    for (std::size_t i = 0u; i < m_Clusters.size(); ++i)
    {
        m_Clusters[i].dataType(dataType);
    }
}

void CKMeansOnline1d::decayRate(double decayRate)
{
    for (std::size_t i = 0u; i < m_Clusters.size(); ++i)
    {
        m_Clusters[i].decayRate(decayRate);
    }
}

bool CKMeansOnline1d::hasCluster(std::size_t index) const
{
    return index < m_Clusters.size();
}

bool CKMeansOnline1d::clusterCentre(std::size_t index, double &result) const
{
    if (!this->hasCluster(index))
    {
        LOG_ERROR("Cluster " << index << " doesn't exist");
        return false;
    }
    result = m_Clusters[index].marginalLikelihoodMean();
    return true;
}

bool CKMeansOnline1d::clusterSpread(std::size_t index, double &result) const
{
    if (!this->hasCluster(index))
    {
        LOG_ERROR("Cluster " << index << " doesn't exist");
        return false;
    }
    result = ::sqrt(m_Clusters[index].marginalLikelihoodVariance());
    return true;
}

void CKMeansOnline1d::cluster(const double &point,
                              TSizeDoublePr2Vec &result,
                              double count) const
{
    result.clear();

    if (m_Clusters.empty())
    {
        LOG_ERROR("No clusters");
        return;
    }

    auto rightCluster = std::lower_bound(m_Clusters.begin(), m_Clusters.end(),
                                         point, detail::SNormalMeanLess());

    if (rightCluster == m_Clusters.end())
    {
        --rightCluster;
        result.emplace_back(rightCluster - m_Clusters.begin(), count);
    }
    else if (rightCluster == m_Clusters.begin())
    {
        result.emplace_back(size_t(0), count);
    }
    else
    {
        auto leftCluster = rightCluster;
        --leftCluster;

        TDouble1Vec sample(1, point);
        double likelihoodLeft  = detail::logLikelihoodFromCluster(sample, *leftCluster);
        double likelihoodRight = detail::logLikelihoodFromCluster(sample, *rightCluster);

        double renormalizer = std::max(likelihoodLeft, likelihoodRight);
        double pLeft  = ::exp(likelihoodLeft - renormalizer);
        double pRight = ::exp(likelihoodRight - renormalizer);
        double normalizer = pLeft + pRight;
        pLeft  /= normalizer;
        pRight /= normalizer;

        if (pLeft < HARD_ASSIGNMENT_THRESHOLD * pRight)
        {
            result.emplace_back(rightCluster - m_Clusters.begin(), count);
        }
        else if (pRight < HARD_ASSIGNMENT_THRESHOLD * pLeft)
        {
            result.emplace_back(leftCluster - m_Clusters.begin(), count);
        }
        else
        {
            result.emplace_back(leftCluster - m_Clusters.begin(), count * pLeft);
            result.emplace_back(rightCluster - m_Clusters.begin(), count * pRight);
        }
    }
}

void CKMeansOnline1d::add(const double &point,
                          TSizeDoublePr2Vec &clusters,
                          double count)
{
    clusters.clear();

    if (m_Clusters.empty())
    {
        return;
    }

    this->cluster(point, clusters, count);

    TDouble1Vec sample{point};
    TDouble4Vec1Vec weight{TDouble4Vec(1)};

    for (std::size_t i = 0u; i < clusters.size(); ++i)
    {
        weight[0][0] = clusters[i].second;
        m_Clusters[clusters[i].first].addSamples(CConstantWeights::COUNT, sample, weight);
    }
}

void CKMeansOnline1d::add(const TDoubleDoublePrVec &points)
{
    TSizeDoublePr2Vec dummy;
    for (std::size_t i = 0u; i < points.size(); ++i)
    {
        this->add(points[i].first, dummy, points[i].second);
    }
}

void CKMeansOnline1d::propagateForwardsByTime(double time)
{
    for (std::size_t i = 0u; i < m_Clusters.size(); ++i)
    {
        m_Clusters[i].propagateForwardsByTime(time);
    }
}

bool CKMeansOnline1d::sample(std::size_t index,
                             std::size_t numberSamples,
                             TDoubleVec &samples) const
{
    if (!this->hasCluster(index))
    {
        LOG_ERROR("Cluster " << index << " doesn't exist");
        return false;
    }
    TDouble1Vec samples_;
    m_Clusters[index].sampleMarginalLikelihood(numberSamples, samples_);
    samples.assign(samples_.begin(), samples_.end());
    return true;
}

double CKMeansOnline1d::probability(std::size_t index) const
{
    if (!this->hasCluster(index))
    {
        return 0.0;
    }
    double weight = m_Clusters[index].numberSamples();
    double weightSum = 0.0;
    for (std::size_t i = 0u; i < m_Clusters.size(); ++i)
    {
        weightSum += m_Clusters[i].numberSamples();
    }
    return weightSum == 0.0 ? 0.0 : weight / weightSum;
}

void CKMeansOnline1d::debugMemoryUsage(core::CMemoryUsage::TMemoryUsagePtr mem) const
{
    mem->setName("CKMeansOnline1d");
    core::CMemoryDebug::dynamicSize("m_Clusters", m_Clusters, mem);
}

std::size_t CKMeansOnline1d::memoryUsage(void) const
{
    return core::CMemory::dynamicSize(m_Clusters);
}

std::size_t CKMeansOnline1d::staticSize(void) const
{
    return sizeof(*this);
}

uint64_t CKMeansOnline1d::checksum(uint64_t seed) const
{
    return CChecksum::calculate(seed, m_Clusters);
}

}
}