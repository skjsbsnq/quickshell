#include <qguiapplication.h>
#include <qtest.h>

#include "commit_atomicity.hpp"
#include "fallback_alpha.hpp"
#include "feedback_lifecycle.hpp"
#include "mapping_lifecycle.hpp"
#include "region_diff.hpp"
#include "transform_lifecycle.hpp"

// Single shared executable for Task 06/13/17/20/08/09 Tahoe glass behavioral tests.
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
	{
		TestFallbackAlpha tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	{
		TestCommitAtomicity tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	{
		TestMappingLifecycle tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	{
		TestFeedbackLifecycle tc;
		status |= QTest::qExec(&tc, argc, argv);
	}
	return status;
}
