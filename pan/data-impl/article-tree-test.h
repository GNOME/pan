#ifndef HEADERS_TEST_H
#define HEADERS_TEST_H

#include <cppunit/extensions/HelperMacros.h>

#include "data-impl.h"

class DataImplTest : public CppUnit::TestFixture {
private:
    SQLiteDb *my_db;
    DataImpl *data;
public:
    void setUp();
    void tearDown() ;
    void testTrue() ;
    DataImplTest() {};
};


#endif // HEADERS_TEST_H
