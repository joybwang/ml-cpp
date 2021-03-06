/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */

#include <config/CFieldStatistics.h>

#include <core/CLogger.h>

#include <config/CAutoconfigurerParams.h>
#include <config/CPenalty.h>

namespace ml {
namespace config {
namespace {

//! \brief Adds an example to the summary statistics.
class CAddToStatistics : public boost::static_visitor<void> {
public:
    CAddToStatistics(core_t::TTime time, const std::string& example)
        : m_Time(time), m_Example(&example) {}

    void operator()(CDataSummaryStatistics& summary) const {
        summary.add(m_Time);
    }

    void operator()(CCategoricalDataSummaryStatistics& summary) const {
        summary.add(m_Time, *m_Example);
    }

    void operator()(CNumericDataSummaryStatistics& summary) const {
        summary.add(m_Time, *m_Example);
    }

private:
    core_t::TTime m_Time;
    const std::string* m_Example;
};
}

CFieldStatistics::CFieldStatistics(const std::string& fieldName,
                                   const CAutoconfigurerParams& params)
    : m_Params(params), m_FieldName(fieldName), m_NumberExamples(0),
      m_Semantics(params.dataType(fieldName)),
      m_SummaryStatistics(CDataSummaryStatistics()) {
}

const std::string& CFieldStatistics::name() const {
    return m_FieldName;
}

void CFieldStatistics::maybeStartCapturingTypeStatistics() {
    if (m_NumberExamples > this->params().minimumExamplesToClassify()) {
        if (const CDataSummaryStatistics* summary = this->summary()) {
            m_Semantics.computeType();

            config_t::EDataType type = m_Semantics.type();

            LOG_DEBUG(<< "Classified '" << m_FieldName << "' as " << config_t::print(type));
            if (config_t::isCategorical(type)) {
                m_SummaryStatistics = CCategoricalDataSummaryStatistics(
                    *summary, this->params().numberOfMostFrequentFieldsCounts());
                this->replayBuffer();
            } else if (config_t::isNumeric(type)) {
                m_SummaryStatistics = CNumericDataSummaryStatistics(
                    *summary, config_t::isInteger(type));
                this->replayBuffer();
            }
        }
    }
}

void CFieldStatistics::add(core_t::TTime time, const std::string& example) {
    ++m_NumberExamples;
    if (m_NumberExamples < this->params().minimumExamplesToClassify()) {
        m_Buffer.push_back(std::make_pair(time, example));
    }
    m_Semantics.add(example);
    boost::apply_visitor(CAddToStatistics(time, example), m_SummaryStatistics);
    this->maybeStartCapturingTypeStatistics();
}

config_t::EDataType CFieldStatistics::type() const {
    return m_Semantics.type();
}

const CDataSummaryStatistics* CFieldStatistics::summary() const {
    return boost::get<CDataSummaryStatistics>(&m_SummaryStatistics);
}

const CCategoricalDataSummaryStatistics* CFieldStatistics::categoricalSummary() const {
    return boost::get<CCategoricalDataSummaryStatistics>(&m_SummaryStatistics);
}

const CNumericDataSummaryStatistics* CFieldStatistics::numericSummary() const {
    return boost::get<CNumericDataSummaryStatistics>(&m_SummaryStatistics);
}

double CFieldStatistics::score(const CPenalty& penalty) const {
    double penality_ = 1.0;
    penalty.penalty(*this, penality_);
    return CPenalty::score(penality_);
}

const CAutoconfigurerParams& CFieldStatistics::params() const {
    return m_Params;
}

void CFieldStatistics::replayBuffer() {
    for (std::size_t i = 0u; i < m_Buffer.size(); ++i) {
        this->add(m_Buffer[i].first, m_Buffer[i].second);
    }
    TTimeStrPrVec empty;
    m_Buffer.swap(empty);
}
}
}
