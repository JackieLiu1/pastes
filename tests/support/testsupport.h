#ifndef PASTES_TESTSUPPORT_H
#define PASTES_TESTSUPPORT_H

#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <stdexcept>

inline void require(bool value, const char *message)
{
	if (!value) throw std::runtime_error(message);
}

inline void waitUntil(const std::function<bool()> &condition, int timeout = 3000)
{
	if (condition()) return;
	QEventLoop loop;
	QTimer poll;
	poll.setInterval(5);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
	QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
	poll.start();
	loop.exec();
	require(condition(), "Timed out waiting for asynchronous completion");
}

inline int runTest(const char *name, const std::function<void()> &test)
{
	try { test(); qInfo() << "PASS" << name; return 0; }
	catch (const std::exception &error) { qCritical() << "FAIL" << name << error.what(); return 1; }
}

#endif
