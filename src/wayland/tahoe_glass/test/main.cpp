#include <qguiapplication.h>
#include <qtest.h>

#include "region_diff.hpp"
#include "transform_lifecycle.hpp"

// Single shared executable for Task 06/13/20 Tahoe glass behavioral tests.
int main(int argc, char** argv) {
	QGuiApplication app(argc, argv);
	int status = 0;
	{
		TestRegionDiff tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	{
		TestTransformLifecycle tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	return status;
}
