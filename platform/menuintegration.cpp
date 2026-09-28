#include "platform/menuintegration.h"

#include <QAction>
#include <QKeySequence>
#include <QWidget>

QAction *Platform::createSettingsAction(QWidget *owner)
{
	auto *action = new QAction(QObject::tr("Settings"), owner);
	if (menuAppearance().standardShortcuts) {
		action->setText(QObject::tr("Preferences…"));
		action->setShortcut(QKeySequence::Preferences);
		owner->addAction(action);
	}
	return action;
}

QAction *Platform::createQuitAction(QWidget *owner)
{
	auto *action = new QAction(QObject::tr("Quit"), owner);
	if (menuAppearance().standardShortcuts) {
		action->setText(QObject::tr("Quit Pastes"));
		action->setShortcut(QKeySequence::Quit);
		owner->addAction(action);
	}
	return action;
}
