/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */

#ifndef INCLUDED_CKdTreeTest_h
#define INCLUDED_CKdTreeTest_h


#include <cppunit/extensions/HelperMacros.h>

class CKdTreeTest : public CppUnit::TestFixture
{
    public:
        void testBuild(void);
        void testNearestNeighbour(void);

        static CppUnit::Test *suite(void);
};

#endif // INCLUDED_CKdTreeTest_h